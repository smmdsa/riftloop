// SQLite persistence. Single local profile, no accounts (goal: local MVP1).
#pragma once
#include "core/ddragon.h"
#include "core/models.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace rl {

struct MatchRow {
    std::string matchId;
    std::string patch;
    std::string queue;
    int         durationSec = 0;
    int64_t     createdMs = 0;
    bool        analyzed = false;
    std::string userChampion;        // convenience for lists
    std::string userRole;
    bool        userWin = false;
};

// One participant of one imported match. No puuid, no name: the meta sample
// never stores who played it.
struct BuildRow {
    std::string patch;
    std::string champion;
    std::string role;
    std::string queue;
    bool        win = false;
    int         durationSec = 0;
    int         primaryStyle = 0;
    int         subStyle = 0;
    std::vector<int> perks;          // 4 primary + 2 secondary
    std::vector<int> items;          // final items, zeros removed
    std::vector<int> spells;
    bool        isUser = false;      // the row the profile owner played
    // Traits of the team this row played against. Lets a query ask for the
    // page most played INTO a comp, not the page most played overall.
    double      enemyBurst = 0;
    double      enemyCc = 0;
    double      enemyMagicShare = 0;
};

// Queue families kept apart: rune pages of ranked, normals and alternate modes
// are not the same sample (PRD 13.3).
enum class QueueFamily { Ranked, Normal, Other };
QueueFamily queueFamilyOf(const std::string& queue);

class Db {
public:
    // Opens (and migrates) the database. Default path: %LOCALAPPDATA%\RiftLoop\riftloop.db
    explicit Db(const std::filesystem::path& path = {});
    ~Db();
    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;

    // profile (row id 1, always present)
    Profile loadProfile();
    void    saveProfile(const Profile& p);

    // matches: raw JSON is kept so the analyzer can re-run with newer rulesets
    bool    upsertMatch(const MatchSummary& m, const std::string& rawMatchJson,
                        const std::string& rawTimelineJson);
    bool    hasMatch(const std::string& matchId);

    // ---- local meta sample (builds of all 10 participants, no identities)
    // dd is optional: without it the matchup context of each row stays zero.
    void    upsertBuilds(const MatchSummary& m, const Ddragon* dd = nullptr);
    // Rebuilds the whole table from the stored match json. Cheap and idempotent;
    // needed after a schema change or for matches imported before it existed.
    int     rebuildBuilds(const Ddragon* dd = nullptr);
    int     buildCount();
    // role or patch empty = no filter on that column. Rows without a role are
    // never returned: a build with no known position is not comparable.
    // Alternate modes (ARAM, Arena) never mix with Summoner's Rift.
    std::vector<BuildRow> builds(const std::string& champion, const std::string& role,
                                 const std::string& patch,
                                 QueueFamily family = QueueFamily::Ranked);
    std::vector<MatchRow> listMatches(int limit = 50);
    std::vector<std::string> pendingAnalysis();
    std::string matchJson(const std::string& matchId);
    // Match ids whose stored json predates a field, e.g. "perks". Used to
    // re-fetch what an older import could not carry.
    std::vector<std::string> matchesMissingField(const std::string& jsonField);
    void    replaceMatchJson(const std::string& matchId, const std::string& rawMatchJson);
    std::string timelineJson(const std::string& matchId);
    // Fills the user columns once the local player is identified.
    void    updateMatchUser(const std::string& matchId, const std::string& champion,
                            const std::string& role, bool win);

    // analysis
    void    saveAnalysis(const AnalysisResult& a);
    std::optional<AnalysisResult> loadAnalysis(const std::string& matchId);
    void    saveFeedback(const std::string& matchId, const std::string& detectorId,
                         const std::string& label, const std::string& reason);

    // missions
    int64_t insertMission(const Mission& m);
    void    updateMission(const Mission& m);
    std::vector<Mission> listMissions();
    std::optional<Mission> activeMission();
    void    insertOpportunity(const Opportunity& o);
    std::vector<Opportunity> opportunitiesFor(int64_t missionId);

    // skill tree + streak + xp
    std::vector<SkillNode> loadSkills();
    void    saveSkill(const SkillNode& n);
    void    recordActivity(const std::string& dayLocal, const std::string& action);
    int     streakDays();            // consecutive days ending today/yesterday
    void    addXp(int amount, const std::string& reason);
    int     totalXp();

    // recommendation contracts (PRD section 30) + audit log
    void    saveRecommendation(const std::string& type, const std::string& contractJson);
    std::string lastRecommendation(const std::string& type);
    void    audit(const std::string& type, const std::string& detailJson);
    std::vector<std::string> auditLog(int limit = 100);

    // kv store (data versions, cached state)
    void    setKv(const std::string& key, const std::string& value);
    std::string getKv(const std::string& key);

    // user rights (PRD 16.4): wipe everything
    void    wipeAll();

private:
    sqlite3* db_ = nullptr;
    void migrate();
    void exec(const std::string& sql);
};

} // namespace rl
