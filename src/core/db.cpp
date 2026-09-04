#include "core/db.h"
#include "core/ingest.h"
#include "core/serial.h"
#include "core/util.h"

#include <sqlite3.h>

#include <stdexcept>

namespace rl {

namespace {

// Minimal statement helper. Binds by index (1-based), reads by column (0-based).
class Stmt {
public:
    Stmt(sqlite3* db, const std::string& sql) {
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st_, nullptr) != SQLITE_OK)
            throw std::runtime_error("sqlite prepare error: " + std::string(sqlite3_errmsg(db)) +
                                     " in: " + sql);
    }
    ~Stmt() { sqlite3_finalize(st_); }
    Stmt& bind(int i, const std::string& v) {
        sqlite3_bind_text(st_, i, v.c_str(), (int)v.size(), SQLITE_TRANSIENT);
        return *this;
    }
    Stmt& bind(int i, int64_t v) { sqlite3_bind_int64(st_, i, v); return *this; }
    Stmt& bind(int i, int v)     { sqlite3_bind_int(st_, i, v); return *this; }
    Stmt& bind(int i, double v)  { sqlite3_bind_double(st_, i, v); return *this; }
    bool step() { return sqlite3_step(st_) == SQLITE_ROW; }
    void run()  { while (sqlite3_step(st_) == SQLITE_ROW) {} }
    std::string text(int c) {
        const unsigned char* t = sqlite3_column_text(st_, c);
        return t ? reinterpret_cast<const char*>(t) : "";
    }
    int64_t i64(int c) { return sqlite3_column_int64(st_, c); }
    int     i32(int c) { return sqlite3_column_int(st_, c); }
    double  f64(int c) { return sqlite3_column_double(st_, c); }

private:
    sqlite3_stmt* st_ = nullptr;
};

} // namespace

Db::Db(const std::filesystem::path& path) {
    auto p = path.empty() ? util::dataDir() / "riftloop.db" : path;
    if (sqlite3_open(p.string().c_str(), &db_) != SQLITE_OK)
        throw std::runtime_error("cannot open database: " + p.string());
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA foreign_keys=ON");
    migrate();
}

Db::~Db() { sqlite3_close(db_); }

void Db::exec(const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown";
        sqlite3_free(err);
        throw std::runtime_error("sqlite exec error: " + msg + " in: " + sql);
    }
}

void Db::migrate() {
    exec(R"sql(
CREATE TABLE IF NOT EXISTS profile(
  id INTEGER PRIMARY KEY CHECK(id=1),
  json TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS matches(
  match_id TEXT PRIMARY KEY,
  patch TEXT, queue TEXT, duration_sec INTEGER, created_ms INTEGER,
  user_champion TEXT, user_role TEXT, user_win INTEGER,
  match_json TEXT NOT NULL, timeline_json TEXT NOT NULL,
  analyzed INTEGER DEFAULT 0);
CREATE TABLE IF NOT EXISTS analysis(
  match_id TEXT PRIMARY KEY,
  ruleset TEXT, created_at TEXT, json TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS feedback(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  match_id TEXT, detector TEXT, label TEXT, reason TEXT, created_at TEXT);
CREATE TABLE IF NOT EXISTS missions(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  detector TEXT, status TEXT, created_at TEXT, json TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS opportunities(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  mission_id INTEGER, match_id TEXT, valid INTEGER, success INTEGER, note TEXT);
CREATE TABLE IF NOT EXISTS skills(
  domain TEXT PRIMARY KEY, json TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS activity(
  day TEXT, action TEXT, created_at TEXT);
CREATE TABLE IF NOT EXISTS xp(
  id INTEGER PRIMARY KEY AUTOINCREMENT, amount INTEGER, reason TEXT, created_at TEXT);
CREATE TABLE IF NOT EXISTS recommendations(
  id INTEGER PRIMARY KEY AUTOINCREMENT, type TEXT, json TEXT, created_at TEXT);
CREATE TABLE IF NOT EXISTS audit(
  id INTEGER PRIMARY KEY AUTOINCREMENT, type TEXT, json TEXT, created_at TEXT);
CREATE TABLE IF NOT EXISTS kv(
  key TEXT PRIMARY KEY, value TEXT);
-- Local meta sample: one row per participant of every imported match. It holds
-- no identity of other players (PRD 16, no scouting): champion, role and build
-- only. This is the legal source for "what wins on this patch" - no scraping.
CREATE TABLE IF NOT EXISTS builds(
  match_id TEXT NOT NULL, participant_id INTEGER NOT NULL,
  patch TEXT, champion TEXT, role TEXT, queue TEXT, win INTEGER,
  duration_sec INTEGER,
  primary_style INTEGER, sub_style INTEGER,
  perks TEXT, items TEXT, spells TEXT,
  PRIMARY KEY(match_id, participant_id));
CREATE INDEX IF NOT EXISTS builds_lookup ON builds(champion, role, patch);
)sql");

    // Additive columns for databases created before they existed. ALTER TABLE
    // fails when the column is already there, which is the normal case.
    for (const char* col : {"is_user INTEGER DEFAULT 0",
                            "enemy_burst REAL DEFAULT 0",
                            "enemy_cc REAL DEFAULT 0",
                            "enemy_magic_share REAL DEFAULT 0"}) {
        try {
            exec(std::string("ALTER TABLE builds ADD COLUMN ") + col);
        } catch (...) {}
    }
}

// ------------------------------------------------------------------- profile

Profile Db::loadProfile() {
    Stmt s(db_, "SELECT json FROM profile WHERE id=1");
    if (s.step()) {
        try { return json::parse(s.text(0)).get<Profile>(); } catch (...) {}
    }
    return {};
}

void Db::saveProfile(const Profile& p) {
    Stmt s(db_, "INSERT INTO profile(id,json) VALUES(1,?1) "
                "ON CONFLICT(id) DO UPDATE SET json=?1");
    s.bind(1, json(p).dump()).run();
}

// ------------------------------------------------------------------- matches

bool Db::upsertMatch(const MatchSummary& m, const std::string& rawMatchJson,
                     const std::string& rawTimelineJson) {
    if (hasMatch(m.matchId)) return false;   // idempotent by match id (PRD 13.3)
    Profile prof = loadProfile();
    const Participant* me = m.byPuuid(prof.puuid);
    Stmt s(db_, "INSERT INTO matches(match_id,patch,queue,duration_sec,created_ms,"
                "user_champion,user_role,user_win,match_json,timeline_json,analyzed) "
                "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,0)");
    s.bind(1, m.matchId).bind(2, m.patch).bind(3, m.queue)
     .bind(4, m.gameDurationSec).bind(5, m.gameCreationMs)
     .bind(6, me ? me->championName : std::string())
     .bind(7, me ? me->position : std::string())
     .bind(8, me && me->win ? 1 : 0)
     .bind(9, rawMatchJson).bind(10, rawTimelineJson);
    s.run();
    upsertBuilds(m, nullptr);
    return true;
}

namespace {

std::string joinInts(const std::vector<int>& v) {
    std::string out;
    for (int x : v) out += (out.empty() ? "" : ",") + std::to_string(x);
    return out;
}

std::vector<int> splitInts(const std::string& csv) {
    std::vector<int> out;
    size_t pos = 0;
    while (pos < csv.size()) {
        size_t next = csv.find(',', pos);
        if (next == std::string::npos) next = csv.size();
        try { out.push_back(std::stoi(csv.substr(pos, next - pos))); } catch (...) {}
        pos = next + 1;
    }
    return out;
}

} // namespace

void Db::upsertBuilds(const MatchSummary& m, const Ddragon* dd) {
    if (m.remake) return;                 // a remake teaches nothing about builds
    std::string myPuuid = loadProfile().puuid;
    for (auto& p : m.participants) {
        if (p.championName.empty()) continue;
        std::vector<int> items;
        for (int id : p.finalItems) if (id > 0) items.push_back(id);

        // Matchup context of this row: the traits of the team it played
        // against. It is what makes "the page most played into this comp"
        // answerable instead of "the page most played, period".
        double burst = 0, cc = 0, magicShare = 0;
        if (dd) {
            std::vector<std::string> enemies;
            for (auto& other : m.participants)
                if (other.teamId != p.teamId && !other.championName.empty())
                    enemies.push_back(other.championName);
            CompTraits t = dd->teamTraits(enemies);
            burst = t.burst;
            cc = t.cc;
            magicShare = t.magical / (t.magical + t.physical + 0.01);
        }

        Stmt s(db_, "INSERT OR REPLACE INTO builds(match_id,participant_id,patch,champion,"
                    "role,queue,win,duration_sec,primary_style,sub_style,perks,items,spells,"
                    "is_user,enemy_burst,enemy_cc,enemy_magic_share) "
                    "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17)");
        s.bind(1, m.matchId).bind(2, p.participantId).bind(3, m.patch)
         .bind(4, p.championName).bind(5, p.position).bind(6, m.queue)
         .bind(7, p.win ? 1 : 0).bind(8, m.gameDurationSec)
         .bind(9, p.perkPrimaryStyle).bind(10, p.perkSubStyle)
         .bind(11, joinInts(p.perks)).bind(12, joinInts(items))
         .bind(13, joinInts({p.summonerSpells[0], p.summonerSpells[1]}))
         .bind(14, (!myPuuid.empty() && p.puuid == myPuuid) ? 1 : 0)
         .bind(15, burst).bind(16, cc).bind(17, magicShare);
        s.run();
    }
}

int Db::rebuildBuilds(const Ddragon* dd) {
    exec("DELETE FROM builds");
    std::vector<std::string> ids;
    {
        Stmt s(db_, "SELECT match_id FROM matches");
        while (s.step()) ids.push_back(s.text(0));
    }
    int rows = 0;
    for (auto& id : ids) {
        auto m = parseMatch(matchJson(id));
        if (!m) continue;
        upsertBuilds(*m, dd);
        rows += (int)m->participants.size();
    }
    return rows;
}

int Db::buildCount() {
    Stmt s(db_, "SELECT COUNT(*) FROM builds");
    return s.step() ? s.i32(0) : 0;
}

QueueFamily queueFamilyOf(const std::string& queue) {
    if (queue.rfind("RANKED", 0) == 0) return QueueFamily::Ranked;
    if (queue.rfind("NORMAL", 0) == 0) return QueueFamily::Normal;
    return QueueFamily::Other;          // ARAM, Arena, rotating modes
}

std::vector<BuildRow> Db::builds(const std::string& champion, const std::string& role,
                                 const std::string& patch, QueueFamily family) {
    Stmt s(db_, "SELECT patch,champion,role,queue,win,duration_sec,primary_style,sub_style,"
                "perks,items,spells,is_user,enemy_burst,enemy_cc,enemy_magic_share FROM builds "
                "WHERE champion=?1 AND role<>'' AND (?2='' OR role=?2) AND (?3='' OR patch=?3)");
    s.bind(1, champion).bind(2, role).bind(3, patch);
    std::vector<BuildRow> out;
    while (s.step()) {
        BuildRow b;
        b.patch = s.text(0);
        b.champion = s.text(1);
        b.role = s.text(2);
        b.queue = s.text(3);
        if (queueFamilyOf(b.queue) != family) continue;
        b.win = s.i32(4) != 0;
        b.durationSec = s.i32(5);
        b.primaryStyle = s.i32(6);
        b.subStyle = s.i32(7);
        b.perks = splitInts(s.text(8));
        b.items = splitInts(s.text(9));
        b.spells = splitInts(s.text(10));
        b.isUser = s.i32(11) != 0;
        b.enemyBurst = s.f64(12);
        b.enemyCc = s.f64(13);
        b.enemyMagicShare = s.f64(14);
        out.push_back(std::move(b));
    }
    return out;
}

std::vector<std::string> Db::matchesMissingField(const std::string& jsonField) {
    Stmt s(db_, "SELECT match_id FROM matches WHERE match_json NOT LIKE ?1");
    s.bind(1, "%\"" + jsonField + "\"%");
    std::vector<std::string> out;
    while (s.step()) out.push_back(s.text(0));
    return out;
}

void Db::replaceMatchJson(const std::string& matchId, const std::string& rawMatchJson) {
    Stmt s(db_, "UPDATE matches SET match_json=?2 WHERE match_id=?1");
    s.bind(1, matchId).bind(2, rawMatchJson);
    s.run();
}

bool Db::hasMatch(const std::string& matchId) {
    Stmt s(db_, "SELECT 1 FROM matches WHERE match_id=?1");
    s.bind(1, matchId);
    return s.step();
}

std::vector<MatchRow> Db::listMatches(int limit) {
    Stmt s(db_, "SELECT match_id,patch,queue,duration_sec,created_ms,analyzed,"
                "user_champion,user_role,user_win FROM matches "
                "ORDER BY created_ms DESC LIMIT ?1");
    s.bind(1, limit);
    std::vector<MatchRow> out;
    while (s.step()) {
        MatchRow r;
        r.matchId = s.text(0); r.patch = s.text(1); r.queue = s.text(2);
        r.durationSec = s.i32(3); r.createdMs = s.i64(4); r.analyzed = s.i32(5) != 0;
        r.userChampion = s.text(6); r.userRole = s.text(7); r.userWin = s.i32(8) != 0;
        out.push_back(std::move(r));
    }
    return out;
}

std::vector<std::string> Db::pendingAnalysis() {
    Stmt s(db_, "SELECT match_id FROM matches WHERE analyzed=0 ORDER BY created_ms");
    std::vector<std::string> out;
    while (s.step()) out.push_back(s.text(0));
    return out;
}

std::string Db::matchJson(const std::string& matchId) {
    Stmt s(db_, "SELECT match_json FROM matches WHERE match_id=?1");
    s.bind(1, matchId);
    return s.step() ? s.text(0) : "";
}

std::string Db::timelineJson(const std::string& matchId) {
    Stmt s(db_, "SELECT timeline_json FROM matches WHERE match_id=?1");
    s.bind(1, matchId);
    return s.step() ? s.text(0) : "";
}

void Db::updateMatchUser(const std::string& matchId, const std::string& champion,
                         const std::string& role, bool win) {
    Stmt s(db_, "UPDATE matches SET user_champion=?1, user_role=?2, user_win=?3 "
                "WHERE match_id=?4");
    s.bind(1, champion).bind(2, role).bind(3, win ? 1 : 0).bind(4, matchId).run();
}

// ------------------------------------------------------------------ analysis

void Db::saveAnalysis(const AnalysisResult& a) {
    {
        Stmt s(db_, "INSERT INTO analysis(match_id,ruleset,created_at,json) VALUES(?1,?2,?3,?4) "
                    "ON CONFLICT(match_id) DO UPDATE SET ruleset=?2,created_at=?3,json=?4");
        s.bind(1, a.matchId).bind(2, a.rulesetVersion).bind(3, util::nowIso())
         .bind(4, json(a).dump()).run();
    }
    Stmt m(db_, "UPDATE matches SET analyzed=1 WHERE match_id=?1");
    m.bind(1, a.matchId).run();
}

std::optional<AnalysisResult> Db::loadAnalysis(const std::string& matchId) {
    Stmt s(db_, "SELECT json FROM analysis WHERE match_id=?1");
    s.bind(1, matchId);
    if (!s.step()) return std::nullopt;
    try { return json::parse(s.text(0)).get<AnalysisResult>(); } catch (...) { return std::nullopt; }
}

void Db::saveFeedback(const std::string& matchId, const std::string& detectorId,
                      const std::string& label, const std::string& reason) {
    Stmt s(db_, "INSERT INTO feedback(match_id,detector,label,reason,created_at) "
                "VALUES(?1,?2,?3,?4,?5)");
    s.bind(1, matchId).bind(2, detectorId).bind(3, label).bind(4, reason)
     .bind(5, util::nowIso()).run();
}

// ------------------------------------------------------------------ missions

int64_t Db::insertMission(const Mission& m) {
    Stmt s(db_, "INSERT INTO missions(detector,status,created_at,json) VALUES(?1,?2,?3,?4)");
    json j(m);
    s.bind(1, m.detectorId).bind(2, j["status"].get<std::string>())
     .bind(3, m.createdAt).bind(4, j.dump()).run();
    return sqlite3_last_insert_rowid(db_);
}

void Db::updateMission(const Mission& m) {
    Stmt s(db_, "UPDATE missions SET status=?1, json=?2 WHERE id=?3");
    json j(m);
    s.bind(1, j["status"].get<std::string>()).bind(2, j.dump()).bind(3, m.dbId).run();
}

std::vector<Mission> Db::listMissions() {
    Stmt s(db_, "SELECT id,json FROM missions ORDER BY id DESC");
    std::vector<Mission> out;
    while (s.step()) {
        try {
            Mission m = json::parse(s.text(1)).get<Mission>();
            m.dbId = s.i64(0);
            out.push_back(std::move(m));
        } catch (...) {}
    }
    return out;
}

std::optional<Mission> Db::activeMission() {
    for (auto& m : listMissions())
        if (m.status == MissionStatus::Active) return m;
    return std::nullopt;
}

void Db::insertOpportunity(const Opportunity& o) {
    Stmt s(db_, "INSERT INTO opportunities(mission_id,match_id,valid,success,note) "
                "VALUES(?1,?2,?3,?4,?5)");
    s.bind(1, o.missionId).bind(2, o.matchId).bind(3, o.valid ? 1 : 0)
     .bind(4, o.success ? 1 : 0).bind(5, o.note).run();
}

std::vector<Opportunity> Db::opportunitiesFor(int64_t missionId) {
    Stmt s(db_, "SELECT match_id,valid,success,note FROM opportunities WHERE mission_id=?1");
    s.bind(1, missionId);
    std::vector<Opportunity> out;
    while (s.step()) {
        Opportunity o;
        o.missionId = missionId;
        o.matchId = s.text(0); o.valid = s.i32(1) != 0;
        o.success = s.i32(2) != 0; o.note = s.text(3);
        out.push_back(std::move(o));
    }
    return out;
}

// ---------------------------------------------------------- skills & streak

std::vector<SkillNode> Db::loadSkills() {
    Stmt s(db_, "SELECT json FROM skills");
    std::vector<SkillNode> out;
    while (s.step()) {
        try { out.push_back(json::parse(s.text(0)).get<SkillNode>()); } catch (...) {}
    }
    return out;
}

void Db::saveSkill(const SkillNode& n) {
    Stmt s(db_, "INSERT INTO skills(domain,json) VALUES(?1,?2) "
                "ON CONFLICT(domain) DO UPDATE SET json=?2");
    s.bind(1, n.domain).bind(2, json(n).dump()).run();
}

void Db::recordActivity(const std::string& dayLocal, const std::string& action) {
    Stmt s(db_, "INSERT INTO activity(day,action,created_at) VALUES(?1,?2,?3)");
    s.bind(1, dayLocal).bind(2, action).bind(3, util::nowIso()).run();
}

int Db::streakDays() {
    Stmt s(db_, "SELECT DISTINCT day FROM activity ORDER BY day DESC LIMIT 400");
    std::vector<std::string> days;
    while (s.step()) days.push_back(s.text(0));
    if (days.empty()) return 0;

    // Walk backwards from today. The streak survives if the last activity was
    // yesterday (today still counts as pending, PRD 9.16).
    auto prevDay = [](std::string d) {
        tm t{};
        sscanf_s(d.c_str(), "%4d-%2d-%2d", &t.tm_year, &t.tm_mon, &t.tm_mday);
        t.tm_year -= 1900; t.tm_mon -= 1; t.tm_hour = 12;
        time_t tt = mktime(&t) - 24 * 3600;
        tm p{};
        localtime_s(&p, &tt);
        char buf[16];
        snprintf(buf, sizeof buf, "%04d-%02d-%02d", p.tm_year + 1900, p.tm_mon + 1, p.tm_mday);
        return std::string(buf);
    };
    std::string expect = util::todayLocal();
    size_t i = 0;
    if (days[0] != expect) {
        expect = prevDay(expect);
        if (days[0] != expect) return 0;
    }
    int streak = 0;
    for (; i < days.size(); ++i) {
        if (days[i] != expect) break;
        ++streak;
        expect = prevDay(expect);
    }
    return streak;
}

void Db::addXp(int amount, const std::string& reason) {
    Stmt s(db_, "INSERT INTO xp(amount,reason,created_at) VALUES(?1,?2,?3)");
    s.bind(1, amount).bind(2, reason).bind(3, util::nowIso()).run();
}

int Db::totalXp() {
    Stmt s(db_, "SELECT COALESCE(SUM(amount),0) FROM xp");
    return s.step() ? s.i32(0) : 0;
}

// ------------------------------------------------- recommendations & audit

void Db::saveRecommendation(const std::string& type, const std::string& contractJson) {
    Stmt s(db_, "INSERT INTO recommendations(type,json,created_at) VALUES(?1,?2,?3)");
    s.bind(1, type).bind(2, contractJson).bind(3, util::nowIso()).run();
}

std::string Db::lastRecommendation(const std::string& type) {
    Stmt s(db_, "SELECT json FROM recommendations WHERE type=?1 ORDER BY id DESC LIMIT 1");
    s.bind(1, type);
    return s.step() ? s.text(0) : "";
}

void Db::audit(const std::string& type, const std::string& detailJson) {
    Stmt s(db_, "INSERT INTO audit(type,json,created_at) VALUES(?1,?2,?3)");
    s.bind(1, type).bind(2, detailJson).bind(3, util::nowIso()).run();
}

std::vector<std::string> Db::auditLog(int limit) {
    Stmt s(db_, "SELECT created_at || ' ' || type || ' ' || json FROM audit "
                "ORDER BY id DESC LIMIT ?1");
    s.bind(1, limit);
    std::vector<std::string> out;
    while (s.step()) out.push_back(s.text(0));
    return out;
}

// ------------------------------------------------------------------------ kv

void Db::setKv(const std::string& key, const std::string& value) {
    Stmt s(db_, "INSERT INTO kv(key,value) VALUES(?1,?2) "
                "ON CONFLICT(key) DO UPDATE SET value=?2");
    s.bind(1, key).bind(2, value).run();
}

std::string Db::getKv(const std::string& key) {
    Stmt s(db_, "SELECT value FROM kv WHERE key=?1");
    s.bind(1, key);
    return s.step() ? s.text(0) : "";
}

void Db::wipeAll() {
    for (const char* t : {"profile", "matches", "analysis", "feedback", "missions",
                          "opportunities", "skills", "activity", "xp",
                          "recommendations", "audit", "kv"})
        exec(std::string("DELETE FROM ") + t);
}

} // namespace rl
