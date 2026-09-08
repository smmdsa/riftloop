// Core test suite. Builds synthetic match/timeline fixtures and checks the
// detector, mission, recommendation and planner rules end to end.
#include "core/analysis.h"
#include "core/config.h"
#include "core/contracts.h"
#include "core/db.h"
#include "core/ddragon.h"
#include "core/detectors.h"
#include "core/fixture.h"
#include "core/curves.h"
#include "core/heatmap.h"
#include "core/ingest.h"
#include "core/ipc.h"
#include "core/lcu.h"
#include "core/meta.h"
#include "core/perkpages.h"
#include "core/replays.h"
#include "core/rivalrank.h"
#include "core/rofl.h"
#include "core/videocut.h"
#include "core/lcu_history.h"
#include "core/missions.h"
#include "core/patchimpact.h"
#include "core/planner.h"
#include "core/podium.h"
#include "core/rank.h"
#include "core/recommend.h"
#include "core/serial.h"
#include "core/util.h"

#include <windows.h>

#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;
using namespace rl;
using nlohmann::json;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        ++g_checks;                                                                  \
        if (!(cond)) {                                                               \
            ++g_failures;                                                            \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);              \
        }                                                                            \
    } while (0)

#define CHECK_EQ(a, b)                                                               \
    do {                                                                             \
        ++g_checks;                                                                  \
        if (!((a) == (b))) {                                                         \
            ++g_failures;                                                            \
            std::printf("FAIL %s:%d  %s == %s\n", __FILE__, __LINE__, #a, #b);       \
        }                                                                            \
    } while (0)

// Index that never leaves the vector. A vector shorter than the test expects
// must print its FAIL line. A raw v[i] past the end aborts the process in the
// MSVC debug runtime, and stdout dies with it, so the report says nothing.
template <class T>
static T at(const std::vector<T>& v, size_t i) {
    return i < v.size() ? v[i] : T{};
}

// ------------------------------------------------------------------ fixtures

static const char* kMyPuuid = "me-puuid";

// Synthetic match: user = participant 1, Ahri MIDDLE, blue team.
// Enemy team includes two heavy healers (Aatrox, Soraka) to trigger D09.
static json makeMatchJson(const std::string& matchId) {
    json info;
    info["gameVersion"] = "16.17.702.1234";
    info["gameCreation"] = 1756800000000LL;
    info["gameDuration"] = 1800;         // 30 min
    info["queueId"] = 420;
    const char* champs[10] = {"Ahri",   "Garen",  "LeeSin", "Jinx",   "Thresh",
                              "Aatrox", "Soraka", "Zed",    "Caitlyn", "Leona"};
    const char* pos[10] = {"MIDDLE", "TOP", "JUNGLE", "BOTTOM", "UTILITY",
                           "TOP", "UTILITY", "MIDDLE", "BOTTOM", "JUNGLE"};
    for (int i = 0; i < 10; ++i) {
        json p;
        p["participantId"] = i + 1;
        p["puuid"] = i == 0 ? kMyPuuid : "p" + std::to_string(i + 1);
        p["riotIdGameName"] = "Player" + std::to_string(i + 1);
        p["riotIdTagline"] = "LAS";
        p["championName"] = champs[i];
        p["championId"] = 100 + i;
        p["teamId"] = i < 5 ? 100 : 200;
        p["teamPosition"] = pos[i];
        p["win"] = i >= 5;
        p["kills"] = 3; p["deaths"] = i == 0 ? 7 : 4; p["assists"] = 5;
        p["goldEarned"] = 11000;
        p["totalMinionsKilled"] = i == 0 ? 150 : 120;
        p["neutralMinionsKilled"] = 0;
        p["champLevel"] = 15;
        p["summoner1Id"] = 4; p["summoner2Id"] = 12;
        for (int k = 0; k < 6; ++k) p["item" + std::to_string(k)] = 0;
        info["participants"].push_back(p);
    }
    json j;
    j["metadata"]["matchId"] = matchId;
    j["info"] = info;
    return j;
}

static json killEvent(int64_t ts, int killer, int victim, std::vector<int> assists = {}) {
    json e;
    e["type"] = "CHAMPION_KILL";
    e["timestamp"] = ts;
    e["killerId"] = killer;
    e["victimId"] = victim;
    e["assistingParticipantIds"] = assists;
    return e;
}

static json makeTimelineJson() {
    json frames = json::array();
    for (int min = 0; min <= 30; ++min) {
        json fr;
        fr["timestamp"] = (int64_t)min * 60000;
        for (int pid = 1; pid <= 10; ++pid) {
            json pf;
            // User carries too much gold at minute 15+ (D03/D04 trigger).
            pf["totalGold"] = 500 * min;
            pf["currentGold"] = (pid == 1 && min >= 15) ? 1800 : 400;
            // CS: strong until 14, collapses afterwards (D06 trigger).
            pf["minionsKilled"] = pid == 1 ? (min <= 14 ? 8 * min : 112 + 2 * (min - 14))
                                           : 6 * min;
            pf["jungleMinionsKilled"] = 0;
            pf["level"] = std::min(18, 1 + min / 2);
            fr["participantFrames"][std::to_string(pid)] = pf;
        }
        fr["events"] = json::array();
        frames.push_back(fr);
    }
    // Two early deaths (D01): minute 3 gank (2 involved), minute 6 solo.
    frames[3]["events"].push_back(killEvent(3 * 60000 + 5000, 8, 1, {6}));
    frames[6]["events"].push_back(killEvent(6 * 60000 + 30000, 8, 1));
    // Death 60 s before enemy dragon at 20:00 (D02) with 1800 gold (D03).
    frames[19]["events"].push_back(killEvent(19 * 60000, 6, 1, {7, 8}));
    json elite;
    elite["type"] = "ELITE_MONSTER_KILL";
    elite["timestamp"] = 20 * 60000;
    elite["killerTeamId"] = 200;
    elite["monsterType"] = "DRAGON";
    frames[20]["events"].push_back(elite);
    // A user kill at 10:00 with no conversion in 75 s (D05).
    frames[10]["events"].push_back(killEvent(10 * 60000, 1, 8));
    // No anti-heal purchase in the whole game (D09 vs Aatrox+Soraka).

    json j;
    j["metadata"]["matchId"] = "TEST_1";
    j["info"]["frames"] = frames;
    return j;
}

static Profile makeProfile() {
    Profile p;
    p.riotId = "Tester#LAS";
    p.puuid = kMyPuuid;
    p.mode = AppMode::Escalar;
    p.preferredRoles = {"MIDDLE"};
    p.pool = {
        {"Ahri", "MIDDLE", PoolTier::Main, 30},
        {"Orianna", "MIDDLE", PoolTier::Comfort, 12},
        {"Galio", "MIDDLE", PoolTier::Comfort, 8},
        {"Yasuo", "MIDDLE", PoolTier::Learning, 2},
        {"Azir", "MIDDLE", PoolTier::DoNotRecommend, 0},
    };
    return p;
}

// ---------------------------------------------------------------------- main

int main() {
    // --- util ---------------------------------------------------------------
    CHECK_EQ(util::patchFamily("16.17.702.1234"), std::string("16.17"));
    CHECK_EQ(util::formatGameClock(1122000), std::string("18:42"));

    // --- ddragon (offline fixtures) -----------------------------------------
    Ddragon dd;
    bool ddOk = dd.loadFromDir(RL_FIXTURES_DIR "/ddragon");
    CHECK(ddOk);
    if (ddOk) {
        CHECK_EQ(dd.version(), std::string("16.17.1"));
        CHECK(dd.champion("Ahri") != nullptr);
        CHECK(dd.champion("Thresh") != nullptr);
        CHECK(dd.item(3111) != nullptr);         // Mercury's Treads
        CHECK(!dd.runeStyles().empty());
        CHECK(dd.summonerKey("SummonerFlash").has_value());
        CHECK(dd.championHealsHeavily("Soraka"));
        // Localized display names for runes/spells (user request: no raw ids).
        CHECK_EQ(dd.perkName(8112), std::string("Electrocute"));
        CHECK_EQ(dd.styleName(8100), std::string("Domination"));
        CHECK_EQ(dd.summonerNameByKey(4), std::string("Flash"));
        CHECK_EQ(dd.summonerDisplay("Cleanse"), std::string("Cleanse"));   // SummonerBoost
        CHECK(!dd.shardName(5008).empty());
        CHECK(dd.itemIconUrl(3111).find("/img/item/3111.png") != std::string::npos);
        // A match payload carries the numeric key, not the ddragon id. Using
        // the localized name here left every summoner spell slot empty.
        CHECK(dd.spellIconUrlByKey(4).find("/img/spell/SummonerFlash.png") != std::string::npos);
        CHECK(dd.spellIconUrlByKey(0).empty());
        CHECK(dd.spellIconUrlByKey(9999).empty());
        CHECK(dd.perkIconUrl(8112).find("https://") == 0);
        CompTraits enemy = dd.teamTraits({"Aatrox", "Soraka", "Zed", "Caitlyn", "Leona"});
        CHECK(enemy.healingShields >= 2.0);
    }

    // --- ingest -------------------------------------------------------------
    std::string matchRaw = makeMatchJson("TEST_1").dump();
    std::string tlRaw = makeTimelineJson().dump();
    auto match = parseMatch(matchRaw);
    CHECK(match.has_value());
    auto tl = parseTimeline(tlRaw, "TEST_1");
    CHECK(tl.has_value());
    if (match && tl) {
        CHECK_EQ(match->patch, std::string("16.17"));
        CHECK_EQ((int)match->participants.size(), 10);
        CHECK(match->byPuuid(kMyPuuid) != nullptr);
        CHECK_EQ((int)tl->frames.size(), 31);
    }

    // --- detectors ----------------------------------------------------------
    if (match && tl && ddOk) {
        DetectorInput in{*match, *tl, 1, &dd};
        auto findings = runDetectors(in);
        auto find = [&](const char* id) -> const Finding* {
            for (auto& f : findings) if (f.detectorId == id) return &f;
            return nullptr;
        };
        const Finding* d01 = find("D01");
        CHECK(d01 && d01->failures == 1 && d01->evidence.size() == 2);
        const Finding* d02 = find("D02");
        CHECK(d02 && d02->failures >= 1);
        const Finding* d03 = find("D03");
        CHECK(d03 && d03->failures >= 1);      // died at 19:00 with 1800 gold
        const Finding* d05 = find("D05");
        CHECK(d05 && d05->opportunities >= 1 && d05->failures >= 1);
        const Finding* d06 = find("D06");
        CHECK(d06 && d06->failures == 1);      // CS collapse after 14
        const Finding* d09 = find("D09");
        CHECK(d09 && d09->failures == 1);      // no anti-heal vs 2 healers
        // Every finding separates facts and inference and has a confidence band.
        for (auto& f : findings) {
            CHECK(!f.confidence.empty());
            for (auto& ev : f.evidence) {
                CHECK(!ev.observedFacts.empty());
                CHECK(ev.source == "timeline");
            }
        }
        CHECK(!detectStrength(in).empty());
    }

    // --- db + analysis + missions ------------------------------------------
    fs::path dbPath = fs::temp_directory_path() / "riftloop_test.db";
    std::error_code ec;
    fs::remove(dbPath, ec);
    {
        Db db(dbPath);
        db.saveProfile(makeProfile());
        Profile p = db.loadProfile();
        CHECK_EQ(p.riotId, std::string("Tester#LAS"));
        CHECK_EQ((int)p.pool.size(), 5);

        CHECK(db.upsertMatch(*match, matchRaw, tlRaw));
        CHECK(!db.upsertMatch(*match, matchRaw, tlRaw));    // idempotent
        CHECK_EQ((int)db.pendingAnalysis().size(), 1);

        auto res = analyzeMatch(db, &dd, "TEST_1");
        CHECK(res.has_value());
        CHECK(db.pendingAnalysis().empty());
        auto loaded = db.loadAnalysis("TEST_1");
        CHECK(loaded && loaded->matchId == "TEST_1" && !loaded->findings.empty());
        CHECK(!loaded->strength.empty());
        CHECK(!loaded->limitations.empty());

        // With one match there are not enough opportunities yet (PRD 12.1).
        CHECK(!suggestMission(db).has_value());

        // Analyze two more matches; the recurring pattern now earns a mission.
        for (int i = 2; i <= 3; ++i) {
            std::string id = "TEST_" + std::to_string(i);
            json mj = makeMatchJson(id);
            auto m2 = parseMatch(mj.dump());
            db.upsertMatch(*m2, mj.dump(), tlRaw);
            analyzeMatch(db, &dd, id);
        }
        auto sug = suggestMission(db);
        CHECK(sug.has_value());
        if (sug) {
            CHECK(!sug->name.empty() && !sug->metric.empty() && !sug->doesNotApply.empty());
            activateMission(db, sug->dbId);
            auto act = db.activeMission();
            CHECK(act.has_value());

            // Track 4 more matches so the block evaluates.
            for (int i = 4; i <= 7; ++i) {
                std::string id = "TEST_" + std::to_string(i);
                json mj = makeMatchJson(id);
                auto m2 = parseMatch(mj.dump());
                db.upsertMatch(*m2, mj.dump(), tlRaw);
                analyzeMatch(db, &dd, id);
            }
            auto missions = db.listMissions();
            bool evaluated = false;
            for (auto& m : missions)
                if (m.dbId == sug->dbId && m.status == MissionStatus::Evaluated) evaluated = true;
            CHECK(evaluated);
            CHECK(!db.loadSkills().empty());
            CHECK(db.totalXp() > 0);
        }

        // --- recommend ------------------------------------------------------
        DraftContext ctx;
        ctx.role = "MIDDLE";
        ctx.setAllyChampions({"Garen", "LeeSin", "Jinx", "Thresh"});
        ctx.setEnemyChampions({"Aatrox", "Soraka", "Zed", "Caitlyn", "Leona"});
        ctx.enemyBans = {"Orianna"};
        Top3 top = recommendTop3(db, dd, ctx);
        CHECK(top.available);
        CHECK((int)top.cards.size() >= 1 && (int)top.cards.size() <= 3);
        for (auto& card : top.cards) {
            CHECK(card.champion != "Azir");      // DoNotRecommend excluded
            CHECK(card.champion != "Yasuo");     // Learning excluded in Escalar
            CHECK(card.champion != "Orianna");   // banned
            CHECK((int)card.reasons.size() <= 3);    // cognitive budget (PRD 10.2)
            CHECK(!card.risk.empty());
            CHECK(!card.confidence.empty());
        }
        CHECK(!top.uncertaintyReason.empty());

        // Aprender mode may include the learning champion.
        Profile p2 = db.loadProfile();
        p2.mode = AppMode::Aprender;
        db.saveProfile(p2);
        Top3 top2 = recommendTop3(db, dd, ctx);
        CHECK(top2.available);
        p2.mode = AppMode::Escalar;
        db.saveProfile(p2);

        // Empty pool for role -> explains instead of inventing (RF-CS-002).
        DraftContext ctxBad = ctx;
        ctxBad.role = "JUNGLE";
        Top3 topBad = recommendTop3(db, dd, ctxBad);
        CHECK(!topBad.available && !topBad.unavailableReason.empty());

        // --- planner --------------------------------------------------------
        PlanInput pi;
        pi.champion = "Ahri";
        pi.role = "MIDDLE";
        pi.draft = ctx;
        RunePlan rp = planRunes(db, dd, pi);
        CHECK(rp.main.perks.size() == 9);
        // All non-shard perks exist in the installed patch (RF-RUN-002).
        for (int i = 0; i < 6; ++i) CHECK(dd.runeExists(rp.main.perks[i]));
        CHECK((int)rp.main.reasons.size() <= 3);

        // --- every generated page must be selectable in the client ----------
        // The bug this guards against: the page suggested Second Wind (8444)
        // and Bone Plating (8473), both row 2 of Resolve. The client cannot
        // select two runes of the same row, so the page was impossible.
        CHECK_EQ(dd.perkSlot(8400, 8444), 2);
        CHECK_EQ(dd.perkSlot(8400, 8473), 2);        // same row: never together
        CHECK_EQ(dd.perkSlot(8400, 8242), 3);
        CHECK_EQ(dd.perkSlot(8000, 8008), 0);        // keystone row
        CHECK_EQ(dd.perkSlot(8000, 9999), -1);       // unknown perk
        CHECK_EQ(dd.perkSlot(9999, 8444), -1);       // unknown style

        auto checkSelectable = [&](const RunePage& page, const char* what) {
            CHECK(page.perks.size() == 9);
            if (page.perks.size() != 9) return;
            // Keystone and the three minors, one per row, in order.
            for (int i = 0; i < 4; ++i) CHECK_EQ(dd.perkSlot(page.primaryStyle, page.perks[i]), i);
            // Two secondary perks, both in the sub style, never the same row.
            int rowA = dd.perkSlot(page.subStyle, page.perks[4]);
            int rowB = dd.perkSlot(page.subStyle, page.perks[5]);
            if (rowA < 1 || rowB < 1 || rowA == rowB)
                std::printf("  pagina no seleccionable (%s): perks %d y %d, filas %d y %d\n",
                            what, page.perks[4], page.perks[5], rowA, rowB);
            CHECK(rowA >= 1);
            CHECK(rowB >= 1);
            CHECK(rowA != rowB);
            // One shard per row.
            const auto& rows = dd.shardRows();
            for (size_t i = 0; i < rows.size(); ++i)
                CHECK(std::find(rows[i].begin(), rows[i].end(), page.perks[6 + i]) !=
                      rows[i].end());
        };

        // Every branch of the generator, against every kind of enemy comp.
        struct Case { const char* label; std::vector<std::string> enemies; };
        const Case cases[] = {
            {"sin comp", {}},
            {"cc pesado", {"Leona", "Thresh", "Sejuani", "Morgana", "Ashe"}},
            {"burst", {"Zed", "Talon", "Rengar", "Katarina", "Nidalee"}},
            {"dps sostenido", {"Jinx", "Kaisa", "Caitlyn", "Vayne", "Ashe"}},
            {"curacion", {"Soraka", "Aatrox", "Yuumi", "Sona", "Nami"}},
        };
        const char* champs[] = {"Ahri", "Garen", "Jinx", "Thresh", "LeeSin", "Zed"};
        const char* roles[] = {"MIDDLE", "TOP", "BOTTOM", "UTILITY", "JUNGLE"};
        for (auto& cs : cases) {
            for (const char* champ : champs) {
                if (!dd.champion(champ)) continue;
                for (const char* role : roles) {
                    PlanInput pv;
                    pv.champion = champ;
                    pv.role = role;
                    pv.draft.role = role;
                    pv.draft.setEnemyChampions(cs.enemies);
                    RunePlan rv = planRunes(db, dd, pv);
                    checkSelectable(rv.main, cs.label);
                    if (rv.situational) checkSelectable(*rv.situational, cs.label);
                    // No champion, role or comp may produce a page with no
                    // intent line, and the two pages never read alike.
                    CHECK(!rv.main.intent.empty());
                    if (rv.situational)
                        CHECK(rv.situational->intent != rv.main.intent);
                }
            }
        }

        // A page that arrives already broken is repaired, not passed through.
        {
            RunePage bad;
            bad.primaryStyle = 8000;
            bad.subStyle = 8400;
            bad.perks = {8008, 9101, 9104, 8017, 8444, 8473, 5008, 5001, 5001};
            bool degraded = false;
            validateRunePage(dd, bad, &degraded);
            CHECK(degraded);
            checkSelectable(bad, "reparada");
        }

        // --- RF-RUN-001: page state follows the draft ------------------------
        // The fixture draft holds all ten champions, so the page is final.
        CHECK(rp.draftClosed);
        CHECK_EQ(rp.missingPicks, 0);
        CHECK(pi.draft.closed());
        // Either an alternative with its own condition, or a stated reason for
        // having none. Never both, never neither (RF-RUN-002).
        CHECK_EQ(rp.situational.has_value(), rp.noAlternativeReason.empty());
        if (rp.situational) {
            CHECK(rp.situational->perks.size() >= 6);
            CHECK(!rp.situational->reasons.empty());
            // An alternative equal to the main page teaches nothing.
            CHECK(rp.situational->perks != rp.main.perks);
            for (int i = 0; i < 6; ++i) CHECK(dd.runeExists(rp.situational->perks[i]));
        }

        // --- TASK-0011: every page says what it goes for ---------------------
        // A page with no intent line leaves the user choosing between two sets
        // of icons. Two pages that say the same thing are the same choice
        // twice, and RF-RUN-002 forbids an alternative that only adds variety.
        CHECK(!rp.main.intent.empty());
        CHECK(rp.main.intent.size() <= 90);
        if (rp.situational) {
            CHECK(!rp.situational->intent.empty());
            CHECK(rp.situational->intent.size() <= 90);
            CHECK(rp.situational->intent != rp.main.intent);
        }
        // The intent is a core value, so it must survive the trip to the UI
        // and to the audit record.
        {
            json wire = rp;
            RunePlan back = wire.get<RunePlan>();
            CHECK_EQ(back.main.intent, rp.main.intent);
            CHECK_EQ(back.situational.has_value(), rp.situational.has_value());
            if (back.situational && rp.situational)
                CHECK_EQ(back.situational->intent, rp.situational->intent);
        }

        // The matchup outranks the sample on the secondary slots, and the
        // stated reason must match the perks that ended up on the page
        // (a page saying "tenacity" without the tenacity perk is a lie).
        PlanInput pcc = pi;
        pcc.draft.setEnemyChampions({"Leona", "Thresh", "Sejuani", "Morgana", "Ashe"});
        RunePlan rcc = planRunes(db, dd, pcc);
        bool claimsTenacity = false;
        for (auto& r : rcc.main.reasons)
            if (r.find("tenacidad") != std::string::npos) claimsTenacity = true;
        if (claimsTenacity) CHECK_EQ(rcc.main.perks[5], 8242);       // Unflinching
        // And the alternative never repeats the main page.
        if (rcc.situational) CHECK(rcc.situational->perks != rcc.main.perks);

        // An open draft never claims to be closed and never promises an
        // alternative it cannot decide yet.
        PlanInput po = pi;
        po.draft.enemySeats[4] = {};
        po.draft.allySeats[3] = {};
        RunePlan ro = planRunes(db, dd, po);
        CHECK(!ro.draftClosed);
        CHECK_EQ(ro.missingPicks, 2);
        CHECK(ro.confidence != "alta");
        CHECK_EQ(ro.situational.has_value(), ro.noAlternativeReason.empty());
        if (!ro.situational)
            CHECK(ro.noAlternativeReason.find("picks") != std::string::npos);

        SpellPlan sp = planSpells(dd, pi);
        CHECK(sp.spells[0] == "Flash");
        CHECK(!sp.reason.empty());
        PlanInput pj = pi;
        pj.role = "JUNGLE";
        CHECK(planSpells(dd, pj).spells[1] == "Smite");     // hard role constraint

        ItemPlan ip = planItems(db, dd, pi);
        CHECK(!ip.starting.empty());
        CHECK((int)ip.branches.size() <= 3);                // overlay budget (RF-OVR-003)
        for (auto& b : ip.branches)
            for (int id : b.items) CHECK(dd.item(id) != nullptr);
        CHECK(!ip.datasetNote.empty());                     // honesty note (RF-ITEM-003)
        bool hasAntiheal = false;
        for (auto& b : ip.branches)
            if (b.label.find("curacion") != std::string::npos) hasAntiheal = true;
        CHECK(hasAntiheal);                                 // 2 healers in enemy comp

        // --- quiz -----------------------------------------------------------
        auto quiz = buildQuiz(dd, pi);
        CHECK(!quiz.empty() && (int)quiz.size() <= 3);      // PRD 10.2
        for (auto& q : quiz) {
            CHECK((int)q.options.size() >= 2 && (int)q.options.size() <= 4);
            CHECK(q.correctIndex >= 0 && q.correctIndex < (int)q.options.size());
            CHECK(!q.explanation.empty());
        }

        // --- contracts ------------------------------------------------------
        json c = makeContract("top3", "16.17", json{{"role", "MIDDLE"}}, json(top),
                              "media", top.uncertaintyReason);
        for (const char* k : {"recommendation_id", "type", "patch", "inputs_used", "options",
                              "confidence", "uncertainty_reason", "model_or_ruleset_version",
                              "expires_at", "policy_mode"})
            CHECK(c.contains(k));
        CHECK_EQ(c["policy_mode"].get<std::string>(), std::string("read_only"));
        db.saveRecommendation("top3", c.dump());
        CHECK(!db.lastRecommendation("top3").empty());

        // --- patch impact ---------------------------------------------------
        auto rep = patchImpact(db, dd);
        CHECK(!rep.entries.empty());
        CHECK(rep.earlyData);

        // --- streak / activity ---------------------------------------------
        db.recordActivity(util::todayLocal(), "quiz_completed");
        CHECK(db.streakDays() >= 1);

        // --- wipe (user rights, PRD 16.4) ----------------------------------
        db.wipeAll();
        CHECK(db.listMatches(10).empty());
        CHECK(db.listMissions().empty());
    }
    fs::remove(dbPath, ec);

    // --- LCU v4 -> v5 converter (client history without API key) ------------
    if (ddOk) {
        json v4game;
        v4game["gameId"] = 987654321;
        v4game["platformId"] = "LA2";
        v4game["gameVersion"] = "16.17.702.1234";
        v4game["gameCreation"] = 1756800000000LL;
        v4game["gameDuration"] = 1900;
        v4game["queueId"] = 420;
        const char* names[4] = {"Ahri", "Garen", "Zed", "Soraka"};
        for (int i = 0; i < 4; ++i) {
            json p;
            p["participantId"] = i + 1;
            p["championId"] = dd.champion(names[i])->key;
            p["teamId"] = i < 2 ? 100 : 200;
            p["spell1Id"] = 4;
            p["spell2Id"] = 12;
            p["timeline"] = {{"lane", i == 3 ? "BOTTOM" : "MIDDLE"},
                             {"role", i == 3 ? "DUO_SUPPORT" : "SOLO"}};
            p["stats"] = {{"win", i < 2}, {"kills", 5}, {"deaths", 3}, {"assists", 7},
                          {"goldEarned", 12000}, {"totalMinionsKilled", 180},
                          {"neutralMinionsKilled", 12}, {"champLevel", 16},
                          {"item0", 3111}, {"item1", 0}, {"item2", 0}, {"item3", 0},
                          {"item4", 0}, {"item5", 0},
                          {"perkPrimaryStyle", 8200}, {"perkSubStyle", 8400},
                          {"perk0", 8229}, {"perk1", 8226}, {"perk2", 8210},
                          {"perk3", 8237}, {"perk4", 8444}, {"perk5", 8473}};
            v4game["participants"].push_back(p);
            json pi;
            pi["participantId"] = i + 1;
            pi["player"] = {{"puuid", std::string("puuid-") + names[i]},
                            {"gameName", names[i]}, {"tagLine", "LAS"}};
            v4game["participantIdentities"].push_back(pi);
        }
        json v5 = convertLcuGameToV5(v4game, &dd);
        CHECK_EQ(v5["metadata"]["matchId"].get<std::string>(), std::string("LA2_987654321"));
        auto conv = parseMatch(v5.dump());
        CHECK(conv.has_value());
        if (conv) {
            CHECK_EQ(conv->patch, std::string("16.17"));
            CHECK_EQ(conv->participants[0].championName, std::string("Ahri"));
            CHECK_EQ(conv->participants[3].position, std::string("UTILITY"));
            CHECK_EQ(conv->participants[0].totalCs, 192);
            CHECK(conv->byPuuid("puuid-Ahri") != nullptr);
        }

        // v4 timeline: BUILDING_KILL carries the DESTROYED team's id; the
        // converter must derive the killer's team.
        json v4tl;
        json fr;
        fr["timestamp"] = 900000;
        fr["participantFrames"]["1"] = {{"totalGold", 5000}, {"currentGold", 500},
                                        {"minionsKilled", 90}, {"level", 9}};
        fr["events"] = json::array();
        fr["events"].push_back({{"type", "BUILDING_KILL"}, {"timestamp", 900000},
                                {"killerId", 0}, {"teamId", 200},
                                {"buildingType", "TOWER_BUILDING"}});
        fr["events"].push_back({{"type", "ELITE_MONSTER_KILL"}, {"timestamp", 910000},
                                {"killerId", 3}, {"monsterType", "DRAGON"}});
        v4tl["frames"].push_back(fr);
        json v5tl = convertLcuTimelineToV5(v4tl, "LA2_987654321", v5);
        auto tconv = parseTimeline(v5tl.dump(), "LA2_987654321");
        CHECK(tconv.has_value());
        if (tconv) {
            CHECK_EQ((int)tconv->events.size(), 2);
            // Blue tower (teamId 200 destroyed) -> killer team 100.
            CHECK_EQ(tconv->events[0].killerTeamId, 100);
            // Dragon killed by participant 3 (team 200).
            CHECK_EQ(tconv->events[1].killerTeamId, 200);
        }
    }

    // --- locale resolution ---------------------------------------------------
    {
        fs::path locPath = fs::temp_directory_path() / "riftloop_locale_test.db";
        std::error_code ec2;
        fs::remove(locPath, ec2);
        Db ldb(locPath);
        CHECK_EQ(resolveDataLocale(ldb, nullptr, "es_MX"), std::string("es_MX"));   // config wins
        CHECK_EQ(resolveDataLocale(ldb, nullptr, ""), std::string("en_US"));        // default
        ldb.setKv("data_locale", "es_AR");
        CHECK_EQ(resolveDataLocale(ldb, nullptr, ""), std::string("es_AR"));        // remembered
        fs::remove(locPath, ec2);
    }

    // --- local meta sample --------------------------------------------------
    {
        fs::path mp = fs::temp_directory_path() / "riftloop_meta_test.db";
        std::error_code ec3;
        fs::remove(mp, ec3);
        Db mdb(mp);

        // Two pages on the same champion: page A in 6 games, page B in 4.
        // Page B wins every game, page A loses every game. Frequency must win:
        // ranking a 4-game sample by win rate is exactly the bias RF-ITEM-003
        // tells us to refuse.
        auto build = [&](const std::string& matchId, int idx, bool pageA, bool win) {
            MatchSummary m;
            m.matchId = matchId;
            m.patch = "16.17";
            m.queue = "RANKED_SOLO_5x5";
            m.gameDurationSec = 1800;
            Participant p;
            p.participantId = idx;
            p.championName = "Ahri";
            p.position = "MIDDLE";
            p.win = win;
            p.perkPrimaryStyle = 8200;
            p.perkSubStyle = pageA ? 8400 : 8100;
            p.perks = pageA ? std::vector<int>{8229, 8226, 8210, 8237, 8444, 8473}
                            : std::vector<int>{8214, 8226, 8210, 8237, 8139, 8135};
            p.finalItems = pageA ? std::vector<int>{3089, 3157, 3111, 0, 0, 0}
                                 : std::vector<int>{3089, 3165, 3111, 0, 0, 0};
            p.summonerSpells = {4, 14};
            m.participants.push_back(p);
            mdb.upsertBuilds(m);
        };

        // Under the minimum sample the meta must stay silent, not guess.
        for (int i = 0; i < 4; ++i) build("M_A" + std::to_string(i), 1, true, false);
        CHECK(!metaRunes(mdb, "Ahri", "MIDDLE", "16.17").has_value());
        CHECK(!metaItems(mdb, dd, "Ahri", "MIDDLE", "16.17").has_value());

        for (int i = 4; i < 6; ++i) build("M_A" + std::to_string(i), 1, true, false);
        for (int i = 0; i < 4; ++i) build("M_B" + std::to_string(i), 1, false, true);
        CHECK_EQ(metaSampleSize(mdb, "Ahri", "MIDDLE", "16.17"), 10);

        auto mr = metaRunes(mdb, "Ahri", "MIDDLE", "16.17");
        CHECK(mr.has_value());
        CHECK_EQ(mr->subStyle, 8400);            // page A: more played, never won
        CHECK_EQ(mr->perks[0], 8229);
        CHECK_EQ((int)mr->perks.size(), 6);
        CHECK_EQ(mr->sample.games, 6);
        CHECK_EQ(mr->sample.wins, 0);
        CHECK_EQ(mr->sample.confidence, std::string("baja"));   // 6 rows is not "media"

        auto mi = metaItems(mdb, dd, "Ahri", "MIDDLE", "16.17");
        CHECK(mi.has_value());
        CHECK(!mi->core.empty());
        CHECK_EQ(mi->core[0], 3089);             // in all 10 rows
        CHECK_EQ((int)mi->boots.size(), 1);
        CHECK_EQ(mi->boots[0], 3111);            // boots never enter the core
        for (int id : mi->core) CHECK(id != 3111);
        CHECK_EQ(mi->sample.games, 10);
        // With 10 rows the note may state a win rate; under 10 it must not.
        CHECK(mi->sample.note().find("victorias") != std::string::npos);
        CHECK(mr->sample.note().find("insuficiente") != std::string::npos);

        // Queue families never mix: a rune page from ARAM is not a rune page
        // for Summoner's Rift (PRD 13.3). Rows without a role are dropped too.
        {
            MatchSummary aram;
            aram.matchId = "M_ARAM";
            aram.patch = "16.17";
            aram.queue = "ARAM";
            aram.gameDurationSec = 1200;
            Participant ap;
            ap.participantId = 1;
            ap.championName = "Ahri";
            ap.position = "MIDDLE";
            ap.perkPrimaryStyle = 8100;
            ap.perkSubStyle = 8300;
            ap.perks = {8112, 8126, 8138, 8135, 8304, 8347};
            ap.finalItems = {3089, 0, 0, 0, 0, 0};
            aram.participants.push_back(ap);
            mdb.upsertBuilds(aram);

            MatchSummary noRole;
            noRole.matchId = "M_NOROLE";
            noRole.patch = "16.17";
            noRole.queue = "RANKED_SOLO_5x5";
            noRole.gameDurationSec = 1800;
            Participant np = ap;
            np.position = "";
            noRole.participants.push_back(np);
            mdb.upsertBuilds(noRole);

            // Neither row reaches the ranked Summoner's Rift sample.
            CHECK_EQ(metaSampleSize(mdb, "Ahri", "MIDDLE", "16.17"), 10);
            CHECK_EQ((int)mdb.builds("Ahri", "MIDDLE", "16.17", QueueFamily::Other).size(), 1);
            CHECK(mdb.builds("Ahri", "", "16.17", QueueFamily::Ranked).size() == 10);
        }

        // Another patch: fall back to the whole sample and flag it (PRD 13.2).
        auto other = metaRunes(mdb, "Ahri", "MIDDLE", "16.18");
        CHECK(other.has_value());
        CHECK(other->sample.fromOlderPatch);
        CHECK_EQ(other->sample.confidence, std::string("baja"));

        // Unknown champion stays silent.
        CHECK(!metaRunes(mdb, "Garen", "TOP", "16.17").has_value());

        // A sample split across many pages answers nothing: no page dominates,
        // so the planner must fall back to the template instead of picking one
        // page out of six with two rows.
        for (int i = 0; i < 6; ++i) {
            MatchSummary m;
            m.matchId = "M_SPLIT" + std::to_string(i);
            m.patch = "16.17";
            m.queue = "RANKED_SOLO_5x5";
            m.gameDurationSec = 1800;
            Participant p;
            p.participantId = 1;
            p.championName = "Zed";
            p.position = "MIDDLE";
            p.perkPrimaryStyle = 8100;
            p.perkSubStyle = 8000 + i;          // a different page every game
            p.perks = {8112 + i, 8126, 8138, 8135, 9111, 9104};
            p.finalItems = {3089, 0, 0, 0, 0, 0};
            m.participants.push_back(p);
            mdb.upsertBuilds(m);
        }
        CHECK_EQ(metaSampleSize(mdb, "Zed", "MIDDLE", "16.17"), 6);
        CHECK(!metaRunes(mdb, "Zed", "MIDDLE", "16.17").has_value());   // no page dominates
        CHECK(metaItems(mdb, dd, "Zed", "MIDDLE", "16.17").has_value());  // items still work

        // A remake teaches nothing and must not enter the sample.
        MatchSummary rm;
        rm.matchId = "M_REMAKE";
        rm.patch = "16.17";
        rm.queue = "RANKED_SOLO_5x5";
        rm.remake = true;
        Participant rp2;
        rp2.participantId = 1;
        rp2.championName = "Ahri";
        rp2.position = "MIDDLE";
        rp2.perks = {8229, 8226, 8210, 8237, 8444, 8473};
        rp2.perkPrimaryStyle = 8200;
        rp2.finalItems = {3089, 0, 0, 0, 0, 0};
        rm.participants.push_back(rp2);
        mdb.upsertBuilds(rm);
        CHECK_EQ(metaSampleSize(mdb, "Ahri", "MIDDLE", "16.17"), 10);

        fs::remove(mp, ec3);
    }

    // --- replay config, metadata and clip planning ---------------------------
    {
        // Real payload shape, taken from a live client.
        std::string cfgJson = R"({"gameVersion":"16.17.810.4348","isInTournament":false,
            "isLoggedIn":true,"isPatching":false,"isPlayingGame":false,"isPlayingReplay":false,
            "isReplaysEnabled":true,"isReplaysForEndOfGameEnabled":true,
            "isReplaysForMatchHistoryEnabled":true,"minutesUntilReplayConsideredLost":30})";
        ReplayConfig rc = parseReplayConfig(cfgJson);
        CHECK(rc.enabled);
        CHECK(rc.forMatchHistory);
        CHECK(!rc.playingGame);
        CHECK_EQ(rc.gameVersion, std::string("16.17.810.4348"));
        // Anything unreadable must read as "not available", never as available.
        CHECK(!parseReplayConfig("").enabled);
        CHECK(!parseReplayConfig("{ broken").enabled);
        CHECK(!parseReplayConfig("{}").enabled);

        // The state spelling has changed between client builds, so unknown
        // strings must not be taken for "the file is ready".
        CHECK(parseReplayMetadata(R"({"state":"notDownloaded"})").state ==
              ReplayState::NotDownloaded);
        CHECK(parseReplayMetadata(R"({"state":"downloading"})").state == ReplayState::Downloading);
        CHECK(parseReplayMetadata(R"({"state":"watch"})").state == ReplayState::Available);
        CHECK(parseReplayMetadata(R"({"state":"incompatible"})").state == ReplayState::Lost);
        CHECK(parseReplayMetadata(R"({"state":"somethingNew"})").state == ReplayState::Unknown);
        CHECK(parseReplayMetadata("").state == ReplayState::Unknown);
        // Payloads taken from a live client, before and after the download.
        // downloadProgress carries an unrelated number while the file is
        // missing, so it must never be shown as a percentage.
        auto before = parseReplayMetadata(
            R"({"downloadProgress":4170411667,"gameId":1621721063,"state":"download"})");
        CHECK(before.state == ReplayState::Downloading);
        CHECK_EQ(before.progressPct, 0);
        auto after = parseReplayMetadata(
            R"({"downloadProgress":100,"gameId":1621721063,"state":"watch"})");
        CHECK(after.state == ReplayState::Available);
        CHECK_EQ(after.progressPct, 100);

        CHECK_EQ(gameIdOfMatch("LA2_1621710462"), 1621710462LL);
        CHECK_EQ(gameIdOfMatch("NA1_42"), 42LL);
        CHECK_EQ(gameIdOfMatch("sin-guion-bajo"), 0LL);
        CHECK_EQ(gameIdOfMatch("LA2_"), 0LL);

        CHECK(!parsePlayback("").valid);
        CHECK(!parsePlayback(R"({"paused":false})").valid);      // no time field
        auto pb = parsePlayback(R"({"length":1800.0,"paused":false,"seeking":false,
                                    "speed":1.0,"time":642.5})");
        CHECK(pb.valid);
        CHECK(pb.timeSec > 642.0 && pb.timeSec < 643.0);
        CHECK(pb.lengthSec > 1799.0);

        // Clip planning: cap at three, lead of 15 s, merge what is too close.
        AnalysisResult ar;
        ar.matchId = "LA2_777";
        auto mkFinding = [](const std::string& id, std::vector<int64_t> stamps, int failures) {
            Finding f;
            f.detectorId = id;
            f.failures = failures;
            for (size_t i = 0; i < stamps.size(); ++i) {
                Evidence e;
                e.evidenceId = id + "-" + std::to_string(i);
                e.matchId = "LA2_777";
                e.gameTimestampMs = stamps[i];
                f.evidence.push_back(e);
            }
            return f;
        };
        ar.findings.push_back(mkFinding("D01", {600'000, 605'000}, 2));   // 5 s apart: one clip
        ar.findings.push_back(mkFinding("D03", {1'200'000}, 1));
        ar.findings.push_back(mkFinding("D07", {90'000}, 1));
        ar.findings.push_back(mkFinding("D05", {1'500'000}, 0));          // no failures: skipped

        auto clips = planClips(ar);
        CHECK_EQ((int)clips.size(), 3);
        // Oldest first.
        CHECK_EQ(clips[0].gameTimestampMs, 90'000LL);
        CHECK_EQ(clips[1].gameTimestampMs, 600'000LL);
        CHECK_EQ(clips[2].gameTimestampMs, 1'200'000LL);
        // The 605 s evidence shares the 600 s clip instead of recording twice.
        for (auto& c : clips) CHECK(c.gameTimestampMs != 605'000LL);
        // A detector with zero failures shows nothing.
        for (auto& c : clips) CHECK(c.detectorId != "D05");
        CHECK_EQ(clips[1].startMs, 600'000LL - kClipLeadMs);
        CHECK_EQ(clips[0].startMs, 90'000LL - kClipLeadMs);
        CHECK_EQ(clips[1].seconds, kClipSeconds);
        // File names reach the file system: only safe characters.
        for (auto& c : clips) {
            CHECK(c.fileName.find(".mp4") != std::string::npos);
            for (char ch : c.fileName)
                CHECK(isalnum((unsigned char)ch) || ch == '.' || ch == '_' || ch == '-');
        }
        // A moment inside the lead window still starts at zero, never negative.
        AnalysisResult early;
        early.matchId = "LA2_778";
        early.findings.push_back(mkFinding("D01", {4'000}, 1));
        auto ec2 = planClips(early);
        CHECK_EQ((int)ec2.size(), 1);
        CHECK_EQ(ec2[0].startMs, 0LL);
        // Nothing to show when no finding failed.
        AnalysisResult clean;
        clean.findings.push_back(mkFinding("D01", {600'000}, 0));
        CHECK(planClips(clean).empty());
        CHECK(planClips(AnalysisResult{}).empty());
    }

    // --- rune page write planning (RF-RUN-003) ------------------------------
    {
        // Payloads with the shape a live client returns.
        std::string pagesJson = R"([
          {"id":710440726,"name":"dianubichardardasarda","primaryStyleId":8000,"subStyleId":8200,
           "selectedPerkIds":[8008,9101,9104,8017,8236,8226,5008,5008,5001],
           "current":true,"isDeletable":true,"isEditable":true,"isTemporary":false,
           "lastModified":1788409697975}])";
        auto pages = parsePerkPages(pagesJson);
        CHECK_EQ((int)pages.size(), 1);
        CHECK_EQ(pages[0].id, 710440726LL);
        CHECK_EQ(pages[0].primaryStyleId, 8000);
        CHECK_EQ((int)pages[0].selectedPerkIds.size(), 9);
        CHECK(pages[0].current);
        CHECK(!pages[0].managed());              // a personal page is not ours
        CHECK(parsePerkPages("").empty());
        CHECK(parsePerkPages("{}").empty());     // an object is not a page list

        auto inv = parsePerkInventory(
            R"({"canAddCustomPage":true,"customPageCount":1,"isCustomPageCreationUnlocked":true,
                "ownedPageCount":2})");
        CHECK(inv.valid);
        CHECK(inv.canAddCustomPage);
        CHECK_EQ(inv.ownedPageCount, 2);
        CHECK(!parsePerkInventory("").valid);

        RunePage wanted;
        wanted.primaryStyle = 8200;
        wanted.subStyle = 8400;
        wanted.perks = {8229, 8226, 8210, 8237, 8444, 8473, 5008, 5008, 5001};

        // No page of ours yet, and there is room: create one.
        auto p1 = planPageWrite(pages, inv, wanted, "RiftLoop: Ahri MIDDLE", "", dd);
        CHECK(p1.action == WriteAction::Create);
        CHECK(!p1.diff.empty());
        CHECK_EQ(p1.targetPageId, 0LL);

        // No room left: blocked, and it must NOT propose deleting a page of
        // the user's. This is the rule that keeps the feature defensible.
        PerkInventory full = inv;
        full.canAddCustomPage = false;
        auto p2 = planPageWrite(pages, full, wanted, "RiftLoop: Ahri MIDDLE", "", dd);
        CHECK(p2.action == WriteAction::Blocked);
        CHECK(p2.blockedReason.find("no borra paginas tuyas") != std::string::npos);

        // With our own page present, overwriting it is allowed and the diff
        // names every slot that changes.
        auto mine = pages;
        PerkPage managed;
        managed.id = 999;
        managed.name = "RiftLoop: Ahri MIDDLE";
        managed.primaryStyleId = 8200;
        managed.subStyleId = 8400;
        managed.selectedPerkIds = {8229, 8226, 8210, 8237, 8444, 8453, 5008, 5008, 5001};
        CHECK(managed.managed());
        mine.push_back(managed);
        std::string appliedSig = pageSignature(managed);
        auto p3 = planPageWrite(mine, inv, wanted, "RiftLoop: Ahri MIDDLE", appliedSig, dd);
        CHECK(p3.action == WriteAction::Overwrite);
        CHECK_EQ(p3.targetPageId, 999LL);
        CHECK_EQ((int)p3.diff.size(), 1);        // only slot 5 differs
        CHECK(!p3.userEdited);

        // Same content: nothing to write.
        RunePage same;
        same.primaryStyle = managed.primaryStyleId;
        same.subStyle = managed.subStyleId;
        same.perks = managed.selectedPerkIds;
        auto p4 = planPageWrite(mine, inv, same, "RiftLoop: Ahri MIDDLE", appliedSig, dd);
        CHECK(p4.action == WriteAction::NoChange);
        CHECK(p4.diff.empty());

        // The user edited our page by hand: do not overwrite it silently.
        auto p5 = planPageWrite(mine, inv, wanted, "RiftLoop: Ahri MIDDLE",
                                pageSignature(wanted), dd);
        CHECK(p5.action == WriteAction::Blocked);
        CHECK(p5.userEdited);
        CHECK_EQ(p5.targetPageId, 999LL);

        // A page the client marks as not editable is left alone.
        auto locked = mine;
        locked.back().isEditable = false;
        auto p6 = planPageWrite(locked, inv, wanted, "RiftLoop: Ahri MIDDLE", appliedSig, dd);
        CHECK(p6.action == WriteAction::Blocked);
        CHECK(!p6.userEdited);

        // An incomplete generated page never reaches the client.
        RunePage broken;
        broken.primaryStyle = 8200;
        broken.subStyle = 8400;
        broken.perks = {8229, 8226};
        auto p7 = planPageWrite(mine, inv, broken, "RiftLoop", appliedSig, dd);
        CHECK(p7.action == WriteAction::Blocked);
        CHECK(!p7.blockedReason.empty());

        // The body carries exactly what the client expects.
        auto body = pageBody(wanted, "RiftLoop: Ahri MIDDLE");
        CHECK_EQ(body.value("primaryStyleId", 0), 8200);
        CHECK_EQ(body.value("subStyleId", 0), 8400);
        CHECK_EQ((int)body["selectedPerkIds"].size(), 9);
        CHECK_EQ(body.value("name", std::string()), std::string("RiftLoop: Ahri MIDDLE"));

        // Signatures detect any change, and only a real one.
        CHECK(pageSignature(wanted) != pageSignature(managed));
        CHECK_EQ(pageSignature(same), pageSignature(managed));
    }

    // --- aligning a recording with the match clock --------------------------
    {
        // A recording starts when the agent notices the game, not at minute
        // zero. Everything below exists so a cut lands on the right second.
        CHECK_EQ(videoPositionSec(90.0, 600'000), 510.0);   // 10:00 of match, 8:30 of video
        CHECK_EQ(videoPositionSec(0.0, 600'000), 600.0);
        // A moment before the recording began yields a negative position, and
        // the caller must skip it instead of cutting from zero.
        CHECK(videoPositionSec(300.0, 120'000) < 0);

        // The two clocks do not share an origin: the Live Client Data API
        // starts counting when the game process starts, the Riot timeline when
        // the match clock hits 0:00. Ignoring the gap put a clip 153 s early.
        // Recording began at API second 200, the match clock started at API
        // second 150, so timeline 600 s is API second 750, i.e. 550 s of video.
        CHECK_EQ(videoPositionSec(200.0, 600'000, 150.0), 550.0);
        // A missing offset must behave exactly like the old two-argument call
        // instead of guessing a number.
        CHECK_EQ(videoPositionSec(200.0, 600'000, -1.0),
                 videoPositionSec(200.0, 600'000));
        CHECK_EQ(videoPositionSec(0.0, 0, 153.0), 153.0);

        std::vector<RecordingInfo> recs;
        RecordingInfo a;
        a.path = L"a.mp4";
        a.startedAtMs = 1'000'000;
        a.valid = true;
        RecordingInfo b;
        b.path = L"b.mp4";
        b.startedAtMs = 1'000'000 + 900'000;      // 15 min later, another match
        b.valid = true;
        recs = {b, a};                            // order must not matter

        // Match created at 1'000'000 lasting 20 min: only the first recording
        // falls inside it.
        CHECK(pickRecordingFor(recs, 1'000'000, 1200) == std::wstring(L"a.mp4"));
        // A match that started later picks the later recording.
        CHECK(pickRecordingFor(recs, 1'000'000 + 900'000, 1200) == std::wstring(L"b.mp4"));
        // Nothing recorded during that match.
        CHECK(pickRecordingFor(recs, 5'000'000, 1200).empty());
        CHECK(pickRecordingFor({}, 1'000'000, 1200).empty());

        // A recording whose sidecar is missing or unreadable is never used:
        // without the game clock it cannot be aligned.
        RecordingInfo bad = readRecordingSidecar(L"no-existe-en-ningun-sitio.mp4");
        CHECK(!bad.valid);
        CHECK(bad.startGameTimeSec < 0);
    }

    // --- champ select parsing ----------------------------------------------
    {
        json cs;
        cs["localPlayerCellId"] = 2;
        cs["myTeam"] = json::array({
            json{{"cellId", 0}, {"championId", 0}, {"championPickIntent", 64}},    // hover
            json{{"cellId", 1}, {"championId", 412}, {"championPickIntent", 0}},   // locked
            json{{"cellId", 2}, {"championId", 0}, {"championPickIntent", 103},
                 {"assignedPosition", "bottom"}},                                  // me, hovering
        });
        cs["theirTeam"] = json::array({
            json{{"cellId", 5}, {"championId", 157}},
            json{{"cellId", 6}, {"championId", 0}},                                // unknown yet
        });
        cs["bans"] = json{{"myTeamBans", json::array({266})}, {"theirTeamBans", json::array()}};
        cs["actions"] = json::array({json::array({
            json{{"type", "ban"}, {"championId", 266}, {"completed", true}, {"actorCellId", 0}},
            json{{"type", "ban"}, {"championId", 84}, {"completed", true}, {"actorCellId", 5}},
            json{{"type", "ban"}, {"championId", 99}, {"completed", false}},       // still hovering
            json{{"type", "pick"}, {"championId", 412}, {"completed", true}},
        })});

        auto v = parseChampSelect(cs.dump(), "[1,2,3]");
        CHECK(v.has_value());
        CHECK_EQ(v->assignedRole, std::string("BOTTOM"));      // lowercase -> match-v5 casing
        CHECK_EQ(v->localChampionId(), 0);                     // hovering, not locked
        CHECK_EQ(v->localHoverChampionId(), 103);
        // --- TASK-0016: one seat per cell, empty seats keep their place ------
        CHECK_EQ(v->localSeat, 2);
        CHECK_EQ(v->allySeats[0].championId, 64);              // cell 0 hovers
        CHECK(v->allySeats[0].state == PickState::Hover);
        CHECK_EQ(v->allySeats[1].championId, 412);             // cell 1 locked
        CHECK(v->allySeats[1].state == PickState::Locked);
        CHECK_EQ(v->allySeats[2].championId, 103);             // cell 2 is the user
        CHECK(v->allySeats[2].state == PickState::Hover);
        CHECK_EQ(v->allySeats[3].championId, 0);               // nobody there yet
        CHECK(v->allySeats[3].state == PickState::Empty);
        CHECK_EQ(v->enemySeats[0].championId, 157);
        CHECK(v->enemySeats[0].state == PickState::Locked);
        CHECK_EQ(v->enemySeats[1].championId, 0);              // the empty cell keeps its seat
        CHECK(v->enemySeats[1].state == PickState::Empty);
        // 266 is banned by our side, 84 by theirs, 99 is not completed. Each
        // ban lands on one side only: 266 comes from the summary AND from an
        // action, and it must not show up twice (TASK-0009).
        CHECK_EQ((int)v->allyBanIds.size(), 1);
        CHECK_EQ(at(v->allyBanIds, 0), 266);
        CHECK_EQ((int)v->enemyBanIds.size(), 1);
        CHECK_EQ(at(v->enemyBanIds, 0), 84);
        CHECK(v->unknownBanIds.empty());
        CHECK_EQ((int)v->allBanIds().size(), 2);
        CHECK_EQ((int)v->pickableChampionIds.size(), 3);

        // Bans only in the summary lists still parse, and keep their side.
        json late = cs;
        late.erase("actions");
        auto v2 = parseChampSelect(late.dump(), "");
        CHECK(v2.has_value());
        CHECK_EQ((int)v2->allyBanIds.size(), 1);
        CHECK(v2->enemyBanIds.empty());
        CHECK(v2->unknownBanIds.empty());
        CHECK(v2->pickableChampionIds.empty());

        // Actions without an actor cell name no side. The ban is kept, and no
        // side claims it: the client did not say whose it is.
        json anon = cs;
        anon.erase("bans");
        anon["actions"] = json::array({json::array({
            json{{"type", "ban"}, {"championId", 266}, {"completed", true}},
            json{{"type", "ban"}, {"championId", 84}, {"completed", true}, {"actorCellId", 5}},
        })});
        auto v3 = parseChampSelect(anon.dump(), "");
        CHECK(v3.has_value());
        CHECK(v3->allyBanIds.empty());
        CHECK_EQ((int)v3->enemyBanIds.size(), 1);              // cell 5 is theirTeam
        CHECK_EQ((int)v3->unknownBanIds.size(), 1);
        CHECK_EQ(at(v3->unknownBanIds, 0), 266);
        CHECK_EQ((int)v3->allBanIds().size(), 2);

        // Three bans on our side and two on theirs stay 3 and 2, not 5.
        json full = cs;
        full["bans"] = json{{"myTeamBans", json::array({266, 84, 55})},
                            {"theirTeamBans", json::array({7, 91})}};
        full.erase("actions");
        auto v4 = parseChampSelect(full.dump(), "");
        CHECK(v4.has_value());
        CHECK_EQ((int)v4->allyBanIds.size(), 3);
        CHECK_EQ((int)v4->enemyBanIds.size(), 2);
        CHECK(v4->unknownBanIds.empty());
        CHECK_EQ((int)v4->allBanIds().size(), 5);
        CHECK_EQ(at(v4->allyBanIds, 2), 55);                   // draft order kept per side
        CHECK_EQ(at(v4->enemyBanIds, 1), 91);

        // The side survives the trip into the draft context, and availability
        // still sees every ban.
        DraftContext dctx;
        dctx.allyBans = {"Aatrox", "Camille", "Swain"};
        dctx.enemyBans = {"Jhin", "Ahri"};
        dctx.unknownBans = {"Zed"};
        CHECK_EQ((int)dctx.allBans().size(), 6);
        CHECK_EQ(at(dctx.allBans(), 0), std::string("Aatrox"));
        CHECK_EQ(at(dctx.allBans(), 5), std::string("Zed"));

        // --- TASK-0016: a late pick never moves the ones already there -------
        // The bug this guards: the old parser compacted the list, so a player
        // who locked later entered BEFORE one who locked first, and the
        // portraits on screen jumped sideways.
        {
            json early;
            early["localPlayerCellId"] = 0;
            early["myTeam"] = json::array({
                json{{"cellId", 0}, {"championId", 103}},          // the user, locked
                json{{"cellId", 1}, {"championId", 0}},            // still deciding
                json{{"cellId", 2}, {"championId", 0}},            // still deciding
                json{{"cellId", 3}, {"championId", 412}},          // locked early
                json{{"cellId", 4}, {"championId", 0}},
            });
            auto a = parseChampSelect(early.dump(), "");
            CHECK(a.has_value());
            CHECK_EQ(a->allySeats[3].championId, 412);
            CHECK_EQ(a->allySeats[1].championId, 0);

            // Now cell 1 locks. Cell 3 must not move.
            json late = early;
            late["myTeam"][1]["championId"] = 64;
            auto b = parseChampSelect(late.dump(), "");
            CHECK(b.has_value());
            CHECK_EQ(b->allySeats[3].championId, 412);         // did NOT move
            CHECK_EQ(b->allySeats[1].championId, 64);
            CHECK_EQ(b->localSeat, 0);

            // The client is free to list the team in any order: the seat comes
            // from the cell, not from the position in the array.
            json shuffled = late;
            std::swap(shuffled["myTeam"][0], shuffled["myTeam"][3]);
            auto c = parseChampSelect(shuffled.dump(), "");
            CHECK(c.has_value());
            CHECK_EQ(c->allySeats[3].championId, 412);
            CHECK_EQ(c->allySeats[0].championId, 103);
            CHECK_EQ(c->localSeat, 0);
        }

        // A draft context counts the same picks as before, user excluded.
        {
            DraftContext dc;
            dc.allySeats[0] = {"Ahri", PickState::Locked};        // the user
            dc.localSeat = 0;
            dc.allySeats[2] = {"Garen", PickState::Locked};
            dc.allySeats[3] = {"Jinx", PickState::Hover};
            for (int i = 0; i < 5; ++i)
                dc.enemySeats[i] = {"Zed", PickState::Locked};
            CHECK_EQ((int)dc.allyChampions().size(), 2);         // the user is not in it
            CHECK_EQ(at(dc.allyChampions(), 0), std::string("Garen"));
            CHECK_EQ((int)dc.enemyChampions().size(), 5);
            CHECK_EQ(dc.knownPicks(), 7);
            CHECK(!dc.closed());                                 // two allies short
            dc.allySeats[1] = {"Thresh", PickState::Locked};
            dc.allySeats[4] = {"LeeSin", PickState::Locked};
            CHECK(dc.closed());
            CHECK_EQ(dc.missingPicks(), 0);
        }

        CHECK(!parseChampSelect("", "").has_value());
        CHECK(!parseChampSelect("not json", "").has_value());
    }

    // --- real fixture: the only one with coordinates (TASK-0018) ------------
    {
        std::string raw = util::readFile(RL_FIXTURES_DIR "/sample/real_1.json");
        CHECK(!raw.empty());

        // No identity survives the export. The scrub runs while the file is
        // written, so a leak here is a leak in the repository (PRD 16, 29).
        // The names are enumerated, not blacklisted. A blacklist has to
        // carry the real name to look for it, and this file is public.
        CHECK(raw.find("LA2_") == std::string::npos);
        int nameCount = 0;
        for (size_t i = raw.find("\"riotIdGameName\""); i != std::string::npos;
             i = raw.find("\"riotIdGameName\"", i + 1)) {
            size_t open = raw.find('"', raw.find(':', i) + 1);
            if (open == std::string::npos) break;
            size_t close = raw.find('"', open + 1);
            if (close == std::string::npos) break;
            std::string v = raw.substr(open + 1, close - open - 1);
            // Only "Me" and "Player2".."Player10" are allowed in the file.
            CHECK(v == "Me" || (v.rfind("Player", 0) == 0 && v.size() > 6 &&
                                v != "Player1" &&
                                v.find_first_not_of("0123456789", 6) ==
                                    std::string::npos));
            ++nameCount;
        }
        CHECK(nameCount >= 10);
        std::vector<std::string> puuids;
        for (size_t i = raw.find("\"puuid\""); i != std::string::npos; i = raw.find("\"puuid\"", i + 1)) {
            size_t open = raw.find('"', raw.find(':', i) + 1);
            if (open == std::string::npos) break;
            size_t close = raw.find('"', open + 1);
            if (close == std::string::npos) break;
            std::string v = raw.substr(open + 1, close - open - 1);
            // Only "me-puuid" and "p2".."p10" are allowed to be in the file.
            CHECK(v == "me-puuid" || (v.size() >= 2 && v[0] == 'p' && v != "p1" &&
                                      v.find_first_not_of("0123456789", 1) == std::string::npos));
            puuids.push_back(v);
        }
        CHECK(puuids.size() >= 10);

        auto pair = splitImport(raw);
        CHECK(pair.has_value());
        if (pair) {
            auto m = parseMatch(pair->matchJson);
            CHECK(m.has_value());
            if (m) {
                CHECK_EQ(m->matchId, std::string("REAL_1"));
                CHECK_EQ((int)m->participants.size(), 10);
                CHECK(m->byPuuid("me-puuid") != nullptr);
            }
            // The point of this fixture: coordinates. The synthetic ones have
            // none, so nothing spatial can be tested against them.
            json realTl = json::parse(pair->timelineJson);
            int framePos = 0, eventPos = 0;
            for (auto& f : realTl["info"]["frames"]) {
                for (auto& [pid, pf] : f["participantFrames"].items()) {
                    (void)pid;
                    if (pf.contains("position")) ++framePos;
                }
                for (auto& e : f["events"])
                    if (e.contains("position")) ++eventPos;
            }
            CHECK(framePos >= 1);
            CHECK(eventPos >= 1);
            CHECK_EQ(framePos, 330);
            CHECK_EQ(eventPos, 131);
        }
    }

    // --- anonymizeFixture: the scrub itself (TASK-0018) ----------------------
    {
        json src;
        src["metadata"]["matchId"] = "LA2_9999";
        src["metadata"]["participants"] = { "real-aaa", "real-bbb" };
        json info;
        info["gameVersion"] = "16.17.702.1234";
        info["gameDuration"] = 1800;
        info["queueId"] = 420;
        for (int i = 0; i < 2; ++i) {
            json pa;
            pa["participantId"] = i + 1;
            pa["puuid"] = i == 0 ? "real-aaa" : "real-bbb";
            pa["riotIdGameName"] = i == 0 ? "SecretName" : "OtherName";
            pa["riotIdTagline"] = "SEC";
            pa["summonerName"] = "LegacyName";
            pa["summonerId"] = "sum-1234";
            pa["championName"] = "Ahri";
            pa["championId"] = 103;
            pa["teamId"] = 100;
            pa["teamPosition"] = "MIDDLE";
            pa["win"] = true;
            info["participants"].push_back(pa);
        }
        src["info"] = info;

        auto fx = anonymizeFixture(src.dump(), "", "real-bbb", "SCRUBBED");
        CHECK(fx.ok);
        CHECK_EQ(fx.participants, 2);
        CHECK(fx.text.find("real-aaa") == std::string::npos);
        CHECK(fx.text.find("real-bbb") == std::string::npos);
        CHECK(fx.text.find("SecretName") == std::string::npos);
        CHECK(fx.text.find("OtherName") == std::string::npos);
        CHECK(fx.text.find("LegacyName") == std::string::npos);
        CHECK(fx.text.find("sum-1234") == std::string::npos);
        CHECK(fx.text.find("LA2_9999") == std::string::npos);
        CHECK(fx.text.find("SCRUBBED") != std::string::npos);
        // The user asked for is the one who becomes me-puuid, whatever slot
        // holds him. Everything that is not an identity stays untouched.
        auto scrubbed = parseMatch(json::parse(fx.text)["match"].dump());
        CHECK(scrubbed.has_value());
        if (scrubbed) {
            const Participant* me = scrubbed->byPuuid("me-puuid");
            CHECK(me != nullptr);
            if (me) {
                CHECK_EQ(me->participantId, 2);
                CHECK_EQ(me->championName, std::string("Ahri"));
            }
            CHECK(scrubbed->byPuuid("p2") != nullptr);
        }
        // A payload with no participants says why, and writes nothing.
        auto bad = anonymizeFixture("{}", "", "x", "X");
        CHECK(!bad.ok);
        CHECK(!bad.error.empty());
        CHECK(bad.text.empty());
    }

    // --- the parser keeps position and xp (TASK-0019) ------------------------
    {
        std::string raw = util::readFile(RL_FIXTURES_DIR "/sample/real_1.json");
        auto pair = splitImport(raw);
        CHECK(pair.has_value());
        if (pair) {
            auto realTimeline = parseTimeline(pair->timelineJson, "REAL_1");
            CHECK(realTimeline.has_value());
            if (realTimeline) {
                CHECK_EQ((int)realTimeline->frames.size(), 33);
                // One frame, read straight off the stored json of a real game.
                TLFrame f5 = at(realTimeline->frames, 5);
                CHECK_EQ((int)f5.pos.count(1), 1);
                if (f5.pos.count(1)) {
                    CHECK_EQ(f5.pos.at(1).x, 1365);
                    CHECK_EQ(f5.pos.at(1).y, 10056);
                }
                CHECK_EQ(f5.xp.count(1) ? f5.xp.at(1) : 0, 1846);
                // Lane cs and jungle cs answer different questions, so the
                // parser keeps them apart instead of adding them at read time.
                CHECK_EQ(f5.laneCs.count(1) ? f5.laneCs.at(1) : -1, 27);
                CHECK_EQ(f5.jungleCs.count(1) ? f5.jungleCs.at(1) : -1, 0);
                CHECK_EQ(f5.cs(1), 27);
                CHECK_EQ(f5.level.count(1) ? f5.level.at(1) : 0, 5);

                int framesWithPos = 0, eventsWithPos = 0;
                for (auto& fr : realTimeline->frames) framesWithPos += (int)fr.pos.size();
                for (auto& ev : realTimeline->events) if (ev.posX >= 0) ++eventsWithPos;
                CHECK_EQ(framesWithPos, 330);
                CHECK_EQ(eventsWithPos, 131);
                CHECK_EQ((int)realTimeline->events.size(), 131);
            }
        }

        // A timeline with no position must produce no coordinate at all. (0,0)
        // is a real corner of the Rift, so a zero there would be a lie.
        auto plain = parseTimeline(makeTimelineJson().dump(), "DEMO_1");
        CHECK(plain.has_value());
        if (plain) {
            int leaked = 0;
            for (auto& fr : plain->frames) leaked += (int)fr.pos.size();
            CHECK_EQ(leaked, 0);
            CHECK(!plain->frames.empty());
            if (!plain->frames.empty())
                CHECK(!plain->frames[0].xp.empty());
        }

        // WARD_KILL used to fall into Other and get dropped before storage.
        json wk;
        json fr;
        fr["timestamp"] = 60000;
        fr["participantFrames"] = json::object();
        json e;
        e["type"] = "WARD_KILL";
        e["timestamp"] = 60000;
        e["killerId"] = 3;
        e["position"] = { {"x", 100}, {"y", 200} };
        fr["events"] = json::array({ e });
        wk["info"]["frames"] = json::array({ fr });
        auto wt = parseTimeline(wk.dump(), "WK_1");
        CHECK(wt.has_value());
        if (wt) {
            CHECK_EQ((int)wt->events.size(), 1);
            CHECK(at(wt->events, 0).type == TLType::WardKill);
            CHECK_EQ(at(wt->events, 0).posX, 100);
            CHECK_EQ(at(wt->events, 0).posY, 200);
            CHECK_EQ(at(wt->events, 0).participantId, 3);
        }
    }

    // --- heat map aggregation (TASK-0020) -----------------------------------
    {
        std::string raw = util::readFile(RL_FIXTURES_DIR "/sample/real_1.json");
        auto pair = splitImport(raw);
        CHECK(pair.has_value());
        if (pair) {
            auto m = parseMatch(pair->matchJson);
            auto t = parseTimeline(pair->timelineJson, "REAL_1");
            CHECK(m.has_value());
            CHECK(t.has_value());
            if (m && t) {
                const Participant* me = m->byPuuid("me-puuid");
                CHECK(me != nullptr);
                if (me) {
                    HeatMap h;
                    addMatchHeat(*t, me->participantId, h);

                    // Counted straight off the fixture: the user is participant
                    // 9, and the numbers below come from its CHAMPION_KILL rows.
                    int deaths = 0, kills = 0, assists = 0;
                    for (auto& e : t->events) {
                        if (e.type != TLType::ChampionKill) continue;
                        if (e.victimId == me->participantId) ++deaths;
                        else if (e.participantId == me->participantId) ++kills;
                        else if (std::find(e.assistIds.begin(), e.assistIds.end(),
                                           me->participantId) != e.assistIds.end()) ++assists;
                    }
                    CHECK_EQ(h.count(HeatKind::Death), deaths);
                    CHECK_EQ(h.count(HeatKind::Kill), kills);
                    CHECK_EQ(h.count(HeatKind::Assist), assists);
                    CHECK_EQ((int)h.points.size(), deaths + kills + assists);
                    // Absolute numbers, counted off the fixture by hand. A
                    // derived count would agree with a broken parser: the
                    // client sends "participantId": 0 beside the real
                    // "killerId", and that once turned every kill into none.
                    CHECK_EQ(h.count(HeatKind::Death), 7);
                    CHECK_EQ(h.count(HeatKind::Kill), 4);
                    CHECK_EQ(h.count(HeatKind::Assist), 12);

                    // Early and late split on minute 14, and they add up.
                    CHECK_EQ(h.count(HeatKind::Death, true) + h.count(HeatKind::Death, false),
                             h.count(HeatKind::Death));

                    // Every point sits on the Rift, and none was invented at 0,0
                    // because a coordinate was missing.
                    for (auto& pt : h.points) {
                        CHECK(pt.x >= kRiftMin && pt.x <= kRiftMax);
                        CHECK(pt.y >= kRiftMin && pt.y <= kRiftMax);
                        CHECK_EQ(pt.matchId, std::string("REAL_1"));
                    }
                }
            }
        }

        // The Y axis flips. Without it the blue base lands at the top right
        // and every reading of the map is backwards.
        CanvasPoint origin = projectToCanvas(kRiftMin, kRiftMin, 600);
        CHECK_EQ(origin.x, 0);
        CHECK_EQ(origin.y, 600);              // bottom left of the canvas
        CanvasPoint topRight = projectToCanvas(kRiftMax, kRiftMax, 600);
        CHECK_EQ(topRight.x, 600);
        CHECK_EQ(topRight.y, 0);                   // top right of the canvas
        CanvasPoint blueNexus = projectToCanvas(1500, 1500, 600);
        CHECK(blueNexus.x < 300);
        CHECK(blueNexus.y > 300);
        CanvasPoint redNexus = projectToCanvas(13300, 13300, 600);
        CHECK(redNexus.x > 300);
        CHECK(redNexus.y < 300);
        // A coordinate outside the Rift is clamped, never drawn off canvas.
        CanvasPoint over = projectToCanvas(99999, -50, 600);
        CHECK_EQ(over.x, 600);
        CHECK_EQ(over.y, 600);

        // A timeline with no coordinates yields no points at all.
        auto plain = parseTimeline(makeTimelineJson().dump(), "DEMO_1");
        CHECK(plain.has_value());
        if (plain) {
            HeatMap h;
            addMatchHeat(*plain, 1, h);
            CHECK_EQ((int)h.points.size(), 0);
        }

        // An empty database says why the map is empty. A blank square with no
        // words is the failure this check exists to stop.
        {
            fs::path emptyDb = fs::temp_directory_path() / "riftloop_heat_empty.db";
            std::error_code rmErr;
            fs::remove(emptyDb, rmErr);
            Db db(emptyDb);
            HeatMap h = collectHeat(db, HeatQuery{});
            CHECK(h.points.empty());
            CHECK_EQ(h.matchesRead, 0);
            CHECK(!h.note.empty());
        }

        // A participantId of zero means "unknown player". It must classify
        // nothing rather than claim every kill in the game.
        if (pair) {
            auto t = parseTimeline(pair->timelineJson, "REAL_1");
            if (t) {
                HeatMap h;
                addMatchHeat(*t, 0, h);
                CHECK_EQ((int)h.points.size(), 0);
            }
        }
    }

    // --- gold, cs and xp curves (TASK-0021) ---------------------------------
    {
        fs::path curveDb = fs::temp_directory_path() / "riftloop_curves_test.db";
        std::error_code rmErr;
        fs::remove(curveDb, rmErr);
        Db db(curveDb);

        // The profile has to be there before the first import: upsertMatch
        // stamps the user columns from it, and the band filters on the role.
        Profile prof = db.loadProfile();
        prof.puuid = "me-puuid";
        db.saveProfile(prof);

        std::string raw = util::readFile(RL_FIXTURES_DIR "/sample/real_1.json");
        auto pair = splitImport(raw);
        CHECK(pair.has_value());
        if (pair) {
            auto m = parseMatch(pair->matchJson);
            CHECK(m.has_value());
            if (m) {
                db.upsertMatch(*m, pair->matchJson, pair->timelineJson);

                MatchCurves c = buildCurves(db, "REAL_1");
                CHECK(c.ok);
                CHECK_EQ(c.champion, std::string("Jinx"));

                // The end of the gold curve is the gold of the scoreboard. If
                // these two ever disagree, one of them is reading the wrong
                // participant.
                const Participant* me = m->byPuuid("me-puuid");
                CHECK(me != nullptr);
                if (me) {
                    CHECK_EQ(c.user[(int)Series::Gold].last(), me->goldEarned);
                    CHECK_EQ(c.user[(int)Series::Gold].last(), 11699);
                    CHECK_EQ(c.user[(int)Series::Cs].last(), me->totalCs);
                    CHECK_EQ(c.user[(int)Series::Xp].last(), 14171);
                }
                // 33 frames, but the last two share minute 31: one point per minute.
                CHECK_EQ((int)c.user[(int)Series::Gold].points.size(), 32);
                CHECK_EQ(at(c.user[(int)Series::Gold].points, 0).minute, 0);

                // One match is not a reference. The view says so and still
                // draws the curve, instead of a band that means nothing.
                CHECK_EQ(c.band[0].samples, 0);
                CHECK(c.band[0].median.points.empty());
                CHECK(!c.note.empty());

                // Everything is here except a lane opponent: this fixture has
                // no enemy that shares the position, and the code must leave
                // the field empty rather than pick someone at random.
                for (auto& p : m->participants)
                    if (p.teamId != me->teamId && p.position == me->position)
                        CHECK(!c.laneChampion.empty());
            }
        }

        // Five more copies of the same game under other ids. The median of
        // identical games is the game itself, so the band must land exactly on
        // the curve. It also proves the band exists at all: without a sample
        // the loop below would check nothing and could never turn red.
        {
            std::string raw2 = util::readFile(RL_FIXTURES_DIR "/sample/real_1.json");
            auto p2 = splitImport(raw2);
            CHECK(p2.has_value());
            if (p2) {
                for (int i = 2; i <= 6; ++i) {
                    std::string id = "REAL_" + std::to_string(i);
                    json mj = json::parse(p2->matchJson);
                    json tj = json::parse(p2->timelineJson);
                    mj["metadata"]["matchId"] = id;
                    tj["metadata"]["matchId"] = id;
                    auto copy = parseMatch(mj.dump());
                    CHECK(copy.has_value());
                    if (copy) CHECK(db.upsertMatch(*copy, mj.dump(), tj.dump()));
                }

                MatchCurves c = buildCurves(db, "REAL_1");
                CHECK(c.ok);
                CHECK_EQ(c.band[0].samples, 5);
                CHECK(c.note.empty());
                for (int sIdx = 0; sIdx < kSeriesCount; ++sIdx) {
                    CHECK(!c.band[sIdx].median.points.empty());
                    CHECK_EQ((int)c.band[sIdx].median.points.size(),
                             (int)c.user[sIdx].points.size());
                    int prev = 0;
                    for (size_t k = 0; k < c.band[sIdx].median.points.size(); ++k) {
                        const CurvePoint& med = c.band[sIdx].median.points[k];
                        // Never falls: gold, cs and xp only go up, so a dip in
                        // the median would be an artefact of which games
                        // reached that minute, not a real loss.
                        CHECK(med.value >= prev);
                        prev = med.value;
                        if (k < c.user[sIdx].points.size())
                            CHECK_EQ(med.value, c.user[sIdx].points[k].value);
                    }
                }
            }
        }

        // A match id that is not stored explains itself and draws nothing.
        MatchCurves missing = buildCurves(db, "NOT_A_MATCH");
        CHECK(!missing.ok);
        CHECK(!missing.note.empty());
    }

    // --- rofl metadata reader (TASK-0022) -----------------------------------
    {
        // A minimal replay, built here. A real .rofl weighs 18 MB and names ten
        // people, so none is committed.
        auto makeRofl = [](const std::string& statsJson, const std::string& trailing) {
            std::string bytes = "RIOT";
            bytes += '\x02';
            bytes += '\x00';
            bytes += "16.17.702.1234";
            bytes += std::string(20, '\x00');       // stand-in for the signature
            bytes += std::string(64, '\xAA');       // stand-in for the payload
            json meta;
            meta["gameLength"] = 1748000;
            meta["lastGameChunkId"] = 61;
            meta["lastKeyFrameId"] = 30;
            meta["statsJson"] = statsJson;
            bytes += meta.dump();
            bytes += trailing;                    // the padding a real file has
            return bytes;
        };
        auto player = [](const std::string& puuid, const std::string& name,
                         const std::string& champ, int team, int wards) {
            json p;
            p["PUUID"] = puuid;
            p["NAME"] = name;
            p["RIOT_ID_GAME_NAME"] = name;
            p["RIOT_ID_TAG_LINE"] = "SEC";
            p["SUMMONER_ID"] = "sum-" + name;
            p["SKIN"] = champ;
            p["TEAM"] = team == 100 ? "100" : "200";
            p["TEAM_POSITION"] = "BOTTOM";
            p["WIN"] = team == 100 ? "Win" : "Fail";
            p["WARD_PLACED"] = std::to_string(wards);
            p["SPELL1_CAST"] = "44";
            p["TOTAL_TIME_SPENT_DEAD"] = "257";
            p["VISION_SCORE"] = "17";
            p["NOT_A_METRIC"] = "hola";           // never stored
            return p;
        };

        json stats = json::array();
        stats.push_back(player("mine-puuid", "Me", "Jinx", 100, 12));
        stats.push_back(player("other-puuid", "SecretName", "Lux", 200, 30));
        // Four trailing bytes, exactly what a real replay carries after the
        // block. A parser that must eat its whole input dies on them.
        std::string file = makeRofl(stats.dump(), std::string(4, '\xFF'));

        RoflStats st = parseRoflStats(file, "mine-puuid");
        CHECK(st.ok);
        CHECK(st.error.empty());
        CHECK_EQ(st.gameVersion, std::string("16.17.702.1234"));
        CHECK_EQ((int)(st.gameLengthMs / 1000), 1748);
        CHECK_EQ(st.chunks, 61);
        CHECK_EQ(st.keyFrames, 30);
        CHECK_EQ((int)st.players.size(), 2);

        const RoflPlayer* me = st.user();
        CHECK(me != nullptr);
        if (me) {
            CHECK_EQ(me->champion, std::string("Jinx"));
            CHECK_EQ(me->team, 100);
            CHECK(me->win);
            CHECK_EQ(me->metrics.count("WARD_PLACED") ? me->metrics.at("WARD_PLACED") : -1, 12);
            CHECK_EQ(me->metrics.count("SPELL1_CAST") ? me->metrics.at("SPELL1_CAST") : -1, 44);
            CHECK_EQ((int)me->metrics.count("NOT_A_METRIC"), 0);
            CHECK_EQ((int)me->metrics.count("PUUID"), 0);
        }
        // The other nine keep their champion and their numbers, and lose every
        // trace of who they are (PRD 16, 29).
        CHECK(!at(st.players, 1).isUser);
        CHECK_EQ(at(st.players, 1).champion, std::string("Lux"));
        CHECK_EQ(at(st.players, 1).team, 200);

        // A replay of somebody else's game marks no user, and says nothing
        // about the nine it does not know.
        RoflStats notMine = parseRoflStats(file, "someone-else");
        CHECK(notMine.ok);
        CHECK(notMine.user() == nullptr);

        // Broken inputs explain themselves and read nothing.
        RoflStats notRofl = parseRoflStats("this is not a replay at all", "mine-puuid");
        CHECK(!notRofl.ok);
        CHECK(!notRofl.error.empty());
        CHECK(notRofl.players.empty());

        // A header with no block at all. Built byte by byte: a literal with an
        // embedded NUL would end at the NUL and never reach the header size.
        std::string headerOnly = "RIOT";
        headerOnly += '\x02';
        headerOnly += '\x00';
        headerOnly += "16.17.702.1234";
        headerOnly += std::string(200, '\xAA');
        RoflStats noBlock = parseRoflStats(headerOnly, "mine-puuid");
        CHECK(!noBlock.ok);
        CHECK(!noBlock.error.empty());

        // Truncated in the middle of the block: the braces never balance.
        std::string cut = file.substr(0, file.size() - 400);
        RoflStats truncated = parseRoflStats(cut, "mine-puuid");
        CHECK(!truncated.ok);
        CHECK(!truncated.error.empty());

        RoflStats empty = parseRoflStats("", "mine-puuid");
        CHECK(!empty.ok);

        // The match id maps to the file name of the replay folder.
        fs::path fake = fs::temp_directory_path() / "riftloop_replays";
        std::error_code mkErr;
        fs::create_directories(fake, mkErr);
        fs::path target = fake / "LA2-1606112641.rofl";
        CHECK(util::writeFile(target, file));
        CHECK_EQ(roflPathForMatch("LA2_1606112641", fake).filename().string(),
                 std::string("LA2-1606112641.rofl"));
        CHECK(roflPathForMatch("LA2_9999999999", fake).empty());
        CHECK(roflPathForMatch("no-underscore", fake).empty());

        RoflStats fromFile = readRoflFile(target, "mine-puuid");
        CHECK(fromFile.ok);
        CHECK(fromFile.user() != nullptr);
        RoflStats missing = readRoflFile(fake / "nope.rofl", "mine-puuid");
        CHECK(!missing.ok);
        CHECK(!missing.error.empty());
    }

    // --- D11: death time against the user's own team (TASK-0023) ------------
    {
        // A replay row with only the fields D11 reads.
        auto row = [](bool isUser, int team, int deadSec, int playedSec) {
            RoflPlayer p;
            p.isUser = isUser;
            p.team = team;
            p.champion = "Jinx";
            p.metrics["TOTAL_TIME_SPENT_DEAD"] = deadSec;
            p.metrics["TIME_PLAYED"] = playedSec;
            return p;
        };
        auto build = [&](int myDead, int teamDead, int playedSec) {
            RoflStats st;
            st.ok = true;
            st.players.push_back(row(true, 100, myDead, playedSec));
            for (int i = 0; i < 4; ++i) st.players.push_back(row(false, 100, teamDead, playedSec));
            for (int i = 0; i < 5; ++i) st.players.push_back(row(false, 200, teamDead, playedSec));
            return st;
        };
        auto findD11 = [](const std::vector<Finding>& fs) -> const Finding* {
            for (const auto& f : fs)
                if (f.detectorId == "D11") return &f;
            return nullptr;
        };

        auto d11Match = parseMatch(matchRaw);
        auto d11Tl = parseTimeline(tlRaw, "TEST_1");
        CHECK(d11Match.has_value());
        CHECK(d11Tl.has_value());
        if (d11Match && d11Tl) {
            // 500 s dead of 2000 played is 25 %, and 2.5x the team median.
            RoflStats bad = build(500, 200, 2000);
            DetectorInput in{*d11Match, *d11Tl, 1, nullptr, &bad};
            auto findings = runDetectors(in);
            const Finding* f = findD11(findings);
            CHECK(f != nullptr);
            if (f) {
                CHECK_EQ(f->opportunities, 1);
                CHECK_EQ(f->failures, 1);
                CHECK_EQ((int)f->evidence.size(), 1);
                if (!f->evidence.empty()) {
                    const Evidence& ev = f->evidence[0];
                    // Every field of the contract is filled (PRD 30).
                    CHECK(!ev.evidenceId.empty());
                    CHECK(!ev.matchId.empty());
                    CHECK_EQ(ev.source, std::string("replay"));
                    CHECK(!ev.observedFacts.empty());
                    CHECK(!ev.inference.empty());
                    CHECK_EQ(ev.confidence, std::string("media"));
                    CHECK(!ev.exclusionsChecked.empty());
                    CHECK(ev.gameTimestampMs > 0);
                    // No teammate is named, ever (PRD 9.11).
                    CHECK(ev.observedFacts.find("Player") == std::string::npos);
                    CHECK(ev.observedFacts.find("puuid") == std::string::npos);
                }
                CHECK(f->title.find("equipo") != std::string::npos);
            }

            // Same share, but the whole team died as much: no finding fires.
            RoflStats even = build(500, 480, 2000);
            DetectorInput inEven{*d11Match, *d11Tl, 1, nullptr, &even};
            auto evenFindings = runDetectors(inEven);
            const Finding* fe = findD11(evenFindings);
            CHECK(fe != nullptr);
            if (fe) {
                CHECK_EQ(fe->opportunities, 1);
                CHECK_EQ(fe->failures, 0);
                CHECK(fe->evidence.empty());
            }

            // Far above the team, but only 5 % of the game: not a problem.
            RoflStats small = build(100, 40, 2000);
            DetectorInput inSmall{*d11Match, *d11Tl, 1, nullptr, &small};
            auto smallFindings = runDetectors(inSmall);
            const Finding* fs2 = findD11(smallFindings);
            CHECK(fs2 != nullptr);
            if (fs2) CHECK_EQ(fs2->failures, 0);

            // Under 15 minutes the respawn timer is short: the rule stays out.
            RoflStats shortGame = build(300, 60, 800);
            DetectorInput inShort{*d11Match, *d11Tl, 1, nullptr, &shortGame};
            auto shortFindings = runDetectors(inShort);
            CHECK(findD11(shortFindings) == nullptr);

            // No replay at all: every other detector still runs.
            DetectorInput inNone{*d11Match, *d11Tl, 1, nullptr, nullptr};
            auto without = runDetectors(inNone);
            CHECK(findD11(without) == nullptr);
            CHECK(!without.empty());

            // A team with fewer than three measured mates is not a reference.
            RoflStats thin;
            thin.ok = true;
            thin.players.push_back(row(true, 100, 500, 2000));
            thin.players.push_back(row(false, 100, 100, 2000));
            DetectorInput inThin{*d11Match, *d11Tl, 1, nullptr, &thin};
            auto thinFindings = runDetectors(inThin);
            CHECK(findD11(thinFindings) == nullptr);
        }
    }

    // --- own rank, read from the client (TASK-0024) --------------------------
    {
        // The shape the client answered on 2026-09-07.
        json q;
        q["tier"] = "BRONZE";
        q["division"] = "II";
        q["leaguePoints"] = 34;
        q["wins"] = 164;
        q["losses"] = 186;
        json body;
        body["queueMap"]["RANKED_SOLO_5x5"] = q;
        body["queueMap"]["RANKED_FLEX_SR"] = json::object();

        auto r = parseRankedStats(body.dump());
        CHECK(r.has_value());
        if (r) {
            CHECK(r->ranked());
            CHECK_EQ(r->tier, std::string("BRONZE"));
            CHECK_EQ(r->division, std::string("II"));
            CHECK_EQ(r->leaguePoints, 34);
            CHECK_EQ(r->wins, 164);
            CHECK_EQ(r->losses, 186);
            CHECK(!r->readAtIso.empty());
            CHECK(r->display().find("BRONZE II") != std::string::npos);
            CHECK(r->display().find("34 LP") != std::string::npos);
            // Nothing is derived from the numbers: no rating, no percentage,
            // no estimate of any kind (PRD 7).
            CHECK(r->display().find("MMR") == std::string::npos);
        }

        // A queue the player never placed in answers "NA", not a tier.
        json unranked;
        unranked["queueMap"]["RANKED_SOLO_5x5"]["tier"] = "NA";
        unranked["queueMap"]["RANKED_SOLO_5x5"]["division"] = "NA";
        auto ur = parseRankedStats(unranked.dump());
        CHECK(ur.has_value());
        if (ur) {
            CHECK(!ur->ranked());
            CHECK_EQ(ur->display(), std::string("Sin clasificar"));
        }

        // A queue that is not in the map, and broken input, give nothing.
        CHECK(!parseRankedStats(body.dump(), "RANKED_TFT").has_value());
        CHECK(!parseRankedStats("").has_value());
        CHECK(!parseRankedStats("not json").has_value());
        CHECK(!parseRankedStats("{}").has_value());

        // With no client and nothing stored, the view says so and invents no
        // value. This is the state a first run is in.
        fs::path rankDb = fs::temp_directory_path() / "riftloop_rank_test.db";
        std::error_code rmErr;
        fs::remove(rankDb, rmErr);
        Db db(rankDb);
        RankView empty = currentRankView(db, nullptr);
        CHECK(!empty.known);
        CHECK(!empty.live);
        CHECK(!empty.note.empty());
        CHECK(empty.rank.tier.empty());

        // Once a value is stored, the offline view returns it and dates it.
        json stored;
        stored["tier"] = "BRONZE";
        stored["division"] = "II";
        stored["leaguePoints"] = 34;
        stored["wins"] = 164;
        stored["losses"] = 186;
        stored["queue"] = "RANKED_SOLO_5x5";
        stored["readAtIso"] = "2026-09-07T18:00:00Z";
        db.setKv("rank.solo", stored.dump());
        RankView offline = currentRankView(db, nullptr);
        CHECK(offline.known);
        CHECK(!offline.live);
        CHECK_EQ(offline.rank.tier, std::string("BRONZE"));
        CHECK(offline.note.find("2026-09-07T18:00:00Z") != std::string::npos);
        CHECK(offline.note.find("League cerrado") != std::string::npos);
    }

    // --- rank of the other nine, history only (TASK-0025) --------------------
    {
        CHECK_EQ(platformOfMatch("LA2_1622503726"), std::string("la2"));
        CHECK_EQ(platformOfMatch("EUW1_123"), std::string("euw1"));
        CHECK(platformOfMatch("no-underscore").empty());
        CHECK(platformOfMatch("_leading").empty());
        // 20 calls, 19 waits of 1300 ms, plus a second of slack.
        CHECK_EQ(estimatedRefreshSeconds(10, 1300), 25);
        CHECK_EQ(estimatedRefreshSeconds(1, 1300), 2);
        CHECK_EQ(estimatedRefreshSeconds(0), 0);

        fs::path rvDb = fs::temp_directory_path() / "riftloop_rivalrank_test.db";
        std::error_code rmErr;
        fs::remove(rvDb, rmErr);
        Db db(rvDb);

        // THE POLICY BARRIER. Champion select has no stored match, so an id
        // that is not in the matches table is refused before anything else
        // happens: before the key is read, and before any call goes out
        // (PRD 17.2 marks scouting a hidden identity as red).
        RiotApiConfig api;
        api.apiKey = "RGAPI-would-be-a-real-key";
        auto blocked = refreshMatchRanks(db, "champ-select-session", api);
        CHECK(!blocked.ok);
        CHECK_EQ(blocked.fetched, 0);
        CHECK(blocked.message.find("champion select") != std::string::npos);
        // The same barrier holds for the id shapes a live session could carry.
        for (const char* id : {"", "LA2_0", "0", "current"}) {
            auto b = refreshMatchRanks(db, id, api);
            CHECK(!b.ok);
            CHECK_EQ(b.fetched, 0);
        }

        CHECK(db.upsertMatch(*match, matchRaw, tlRaw));

        // A stored match with no rank read yet: it says how many are missing
        // and offers no average at all.
        MatchRankView none = storedMatchRanks(db, "TEST_1");
        CHECK_EQ(none.known, 0);
        CHECK_EQ(none.missing, 10);
        CHECK(none.averageTier.empty());
        CHECK(none.note.find("10 de 10") != std::string::npos);

        // With no key, a stored match still refuses and explains itself. This
        // is the state the user is in until they set one.
        RiotApiConfig noKey;
        auto needsKey = refreshMatchRanks(db, "TEST_1", noKey);
        CHECK(!needsKey.ok);
        CHECK_EQ(needsKey.fetched, 0);
        CHECK(needsKey.message.find("clave") != std::string::npos);

        // Ten players read: five IRON IV (step 0) and five SILVER IV (step 8).
        // The average of the official steps is 4, which is BRONZE IV.
        for (int pid = 1; pid <= 10; ++pid) {
            RivalRank r;
            r.participantId = pid;
            r.tier = pid <= 5 ? "IRON" : "SILVER";
            r.division = "IV";
            r.leaguePoints = 50;
            r.readAtEpoch = 1757000000;
            db.upsertMatchRank("TEST_1", r);
        }
        MatchRankView full = storedMatchRanks(db, "TEST_1");
        CHECK_EQ(full.known, 10);
        CHECK_EQ(full.missing, 0);
        CHECK_EQ(full.averageTier, std::string("BRONZE"));
        CHECK_EQ(full.averageDivision, std::string("IV"));
        CHECK(full.note.empty());
        const RivalRank* one = full.byParticipant(3);
        CHECK(one != nullptr);
        if (one) {
            CHECK(one->cached());
            CHECK(one->ranked());
            CHECK_EQ(one->display(), std::string("IRON IV · 50 LP"));
        }
        CHECK(full.byParticipant(99) == nullptr);

        // An upsert of the same slot replaces it; it never adds a second row.
        RivalRank climbed;
        climbed.participantId = 3;
        climbed.tier = "GOLD";
        climbed.division = "I";
        climbed.leaguePoints = 12;
        climbed.readAtEpoch = 1757000100;
        db.upsertMatchRank("TEST_1", climbed);
        MatchRankView after = storedMatchRanks(db, "TEST_1");
        CHECK_EQ((int)after.rows.size(), 10);
        const RivalRank* moved = after.byParticipant(3);
        CHECK(moved != nullptr);
        if (moved) CHECK_EQ(moved->tier, std::string("GOLD"));

        // A player read with no rank in this queue counts as read, not missing.
        RivalRank unranked;
        unranked.participantId = 4;
        unranked.readAtEpoch = 1757000200;
        db.upsertMatchRank("TEST_1", unranked);
        MatchRankView mixed = storedMatchRanks(db, "TEST_1");
        CHECK_EQ(mixed.unranked, 1);
        CHECK_EQ(mixed.known, 9);
        CHECK_EQ(mixed.missing, 0);
        CHECK_EQ(mixed.byParticipant(4)->display(), std::string("Sin clasificar"));

        // Under six known ranks there is no average: three of ten averaged and
        // called "the average of the game" would be a lie (PRD 3.3).
        fs::path thinDb = fs::temp_directory_path() / "riftloop_rivalrank_thin.db";
        fs::remove(thinDb, rmErr);
        Db thin(thinDb);
        CHECK(thin.upsertMatch(*match, matchRaw, tlRaw));
        for (int pid = 1; pid <= 3; ++pid) {
            RivalRank r;
            r.participantId = pid;
            r.tier = "GOLD";
            r.division = "II";
            r.readAtEpoch = 1757000000;
            thin.upsertMatchRank("TEST_1", r);
        }
        MatchRankView few = storedMatchRanks(thin, "TEST_1");
        CHECK_EQ(few.known, 3);
        CHECK(few.averageTier.empty());
        CHECK(few.note.find("7 de 10") != std::string::npos);

        // The wipe is total. The rank of other people cannot survive it
        // (PRD 16.4).
        db.wipeAll();
        CHECK(db.matchRanks("TEST_1").empty());
    }

    // --- the podium of one match (TASK-0026) ---------------------------------
    {
        std::string raw = util::readFile(RL_FIXTURES_DIR "/sample/real_1.json");
        auto pair = splitImport(raw);
        CHECK(pair.has_value());
        if (pair) {
            auto m = parseMatch(pair->matchJson);
            CHECK(m.has_value());
            if (m) {
                MatchPodium pod = buildPodium(*m);
                CHECK_EQ((int)pod.entries.size(), 10);

                // Ranks are 1..10, in order, with no gap and no repeat.
                for (size_t i = 0; i < pod.entries.size(); ++i) {
                    CHECK_EQ(pod.entries[i].rank, (int)i + 1);
                    if (i > 0) CHECK(pod.entries[i - 1].score >= pod.entries[i].score);
                }
                // Every participant is on the podium exactly once.
                for (auto& p : m->participants) CHECK(pod.byParticipant(p.participantId) != nullptr);
                CHECK(pod.byParticipant(99) == nullptr);

                // The score is the sum of its own factors, and the weights add
                // up to 100. A total the breakdown cannot explain is a number
                // out of nowhere (PRD 30).
                for (const auto& e : pod.entries) {
                    int sum = 0, weights = 0;
                    for (const auto& f : e.factors) {
                        sum += f.points;
                        weights += f.weight;
                        CHECK(f.points <= f.weight);
                        CHECK(f.points >= 0);
                        CHECK(!f.label.empty());
                        CHECK(!f.detail.empty());
                    }
                    CHECK_EQ(sum, e.score);
                    CHECK_EQ(weights, 100);
                    CHECK(e.score >= 0 && e.score <= 100);
                }

                // The first place always carries the top title.
                CHECK_EQ(at(pod.entries, 0).title, std::string("EL SMURFER"));
                CHECK(!at(pod.entries, 0).titleWhy.empty());

                // MVP-CARRY goes to the best player of the side that lost, and
                // only there.
                std::string carryFor;
                for (const auto& e : pod.entries) {
                    if (e.title != "MVP-CARRY") continue;
                    const Participant* p = m->byId(e.participantId);
                    CHECK(p != nullptr);
                    if (p) {
                        CHECK(!p->win);
                        carryFor = p->championName;
                        // Nobody from the losing side ranks above them.
                        for (const auto& other : pod.entries) {
                            if (other.rank >= e.rank) continue;
                            const Participant* op = m->byId(other.participantId);
                            if (op) CHECK(op->win);
                        }
                    }
                }
                CHECK(!carryFor.empty());

                // A title belongs to one player only, and no player has two.
                std::map<std::string, int> titleCount;
                for (const auto& e : pod.entries)
                    if (!e.title.empty()) ++titleCount[e.title];
                for (auto& [title, n] : titleCount) {
                    CHECK_EQ(n, 1);
                    // No title reads as blame: PRD 9.11 forbids naming a
                    // teammate as a cause, so the bottom rows carry no title.
                    CHECK(title.find("PEOR") == std::string::npos);
                    CHECK(title.find("INUTIL") == std::string::npos);
                    CHECK(title.find("LASTRE") == std::string::npos);
                }
            }
        }

        // Winning is worth points, so two identical lines split on the result.
        // Without it the best of the losing side outranks the winner who did
        // the same, which is what made the old percentage read wrong.
        {
            json info;
            info["gameVersion"] = "16.17.702.1234";
            info["gameDuration"] = 1800;
            info["queueId"] = 420;
            for (int i = 0; i < 2; ++i) {
                json pa;
                pa["participantId"] = i + 1;
                pa["puuid"] = "p" + std::to_string(i + 1);
                pa["championName"] = "Ahri";
                pa["teamId"] = i == 0 ? 100 : 200;
                pa["teamPosition"] = "MIDDLE";
                pa["win"] = i == 0;
                pa["kills"] = 5; pa["deaths"] = 3; pa["assists"] = 5;
                pa["goldEarned"] = 10000;
                pa["totalMinionsKilled"] = 150; pa["neutralMinionsKilled"] = 0;
                pa["champLevel"] = 15;
                info["participants"].push_back(pa);
            }
            json j;
            j["metadata"]["matchId"] = "TIE_1";
            j["info"] = info;
            auto tie = parseMatch(j.dump());
            CHECK(tie.has_value());
            if (tie) {
                MatchPodium pod = buildPodium(*tie);
                CHECK_EQ((int)pod.entries.size(), 2);
                const Participant* first = tie->byId(at(pod.entries, 0).participantId);
                CHECK(first != nullptr);
                if (first) CHECK(first->win);
                CHECK(at(pod.entries, 0).score > at(pod.entries, 1).score);
            }
        }

        // An empty match produces an empty podium instead of a crash.
        MatchSummary blank;
        MatchPodium none = buildPodium(blank);
        CHECK(none.entries.empty());
        CHECK(none.byParticipant(1) == nullptr);
    }

    // --- v4 lane/role -> v5 teamPosition (TASK-0029) -------------------------
    {
        // The exact shape the client answered for LA2_1622561091 on
        // 2026-09-07. Two players a side arrive as lane JUNGLE role NONE, the
        // support arrives as SUPPORT and not DUO_SUPPORT.
        auto player = [](int pid, int team, const char* lane, const char* role,
                         int spell1, int spell2, int neutral, int minions) {
            json p;
            p["participantId"] = pid;
            p["teamId"] = team;
            p["championId"] = 100 + pid;
            p["spell1Id"] = spell1;
            p["spell2Id"] = spell2;
            p["timeline"]["lane"] = lane;
            p["timeline"]["role"] = role;
            p["stats"]["win"] = team == 100;
            p["stats"]["neutralMinionsKilled"] = neutral;
            p["stats"]["totalMinionsKilled"] = minions;
            return p;
        };
        json game;
        game["gameVersion"] = "16.17.702.1234";
        game["gameDuration"] = 1307;
        game["queueId"] = 400;
        game["platformId"] = "LA2";
        game["gameId"] = 1622561091LL;
        game["participants"] = json::array({
            player(1,  100, "JUNGLE", "NONE",    12, 4, 0,   133),
            player(2,  100, "JUNGLE", "NONE",    4, 11, 110, 9),
            player(3,  100, "MIDDLE", "SOLO",    4, 14, 20,  128),
            player(4,  100, "BOTTOM", "CARRY",   7, 4,  0,   246),
            player(5,  100, "BOTTOM", "SUPPORT", 4, 3,  0,   41),
            player(6,  200, "JUNGLE", "NONE",    4, 3,  0,   158),
            player(7,  200, "JUNGLE", "NONE",    11, 4, 132, 23),
            player(8,  200, "MIDDLE", "SOLO",    4, 14, 10,  175),
            player(9,  200, "BOTTOM", "CARRY",   7, 4,  0,   202),
            player(10, 200, "BOTTOM", "SUPPORT", 4, 3,  0,   73),
        });
        game["participantIdentities"] = json::array();

        json v5 = convertLcuGameToV5(game, nullptr);
        auto m = parseMatch(v5.dump());
        CHECK(m.has_value());
        if (m) {
            auto posOf = [&](int pid) {
                const Participant* p = m->byId(pid);
                return p ? p->position : std::string("<missing>");
            };
            // The player with Smite keeps the jungle; the other moves to top.
            CHECK_EQ(posOf(1), std::string("TOP"));
            CHECK_EQ(posOf(2), std::string("JUNGLE"));
            CHECK_EQ(posOf(6), std::string("TOP"));
            CHECK_EQ(posOf(7), std::string("JUNGLE"));
            // SUPPORT, not DUO_SUPPORT: matching only the DUO_ name left the
            // support labelled BOTTOM and UTILITY empty.
            CHECK_EQ(posOf(5), std::string("UTILITY"));
            CHECK_EQ(posOf(10), std::string("UTILITY"));
            CHECK_EQ(posOf(4), std::string("BOTTOM"));
            CHECK_EQ(posOf(3), std::string("MIDDLE"));

            // Each side ends with the five positions, once each.
            for (int team : {100, 200}) {
                std::map<std::string, int> seen;
                for (auto& p : m->participants)
                    if (p.teamId == team) ++seen[p.position];
                for (const char* pos : {"TOP", "JUNGLE", "MIDDLE", "BOTTOM", "UTILITY"})
                    CHECK_EQ(seen[pos], 1);
            }
        }

        // The old DUO_ spelling still works: an older payload must not break.
        json legacy = game;
        legacy["participants"][4]["timeline"]["role"] = "DUO_SUPPORT";
        legacy["participants"][3]["timeline"]["role"] = "DUO_CARRY";
        auto lm = parseMatch(convertLcuGameToV5(legacy, nullptr).dump());
        CHECK(lm.has_value());
        if (lm) {
            CHECK_EQ(lm->byId(5)->position, std::string("UTILITY"));
            CHECK_EQ(lm->byId(4)->position, std::string("BOTTOM"));
        }

        // A side that already has a top keeps it: the second jungle stays put
        // rather than overwriting a position the client did label.
        json withTop = game;
        withTop["participants"][0]["timeline"]["lane"] = "TOP";
        auto tm = parseMatch(convertLcuGameToV5(withTop, nullptr).dump());
        CHECK(tm.has_value());
        if (tm) {
            CHECK_EQ(tm->byId(1)->position, std::string("TOP"));
            CHECK_EQ(tm->byId(2)->position, std::string("JUNGLE"));
        }

        // With no Smite anywhere, the camps decide.
        json noSmite = game;
        noSmite["participants"][1]["spell1Id"] = 4;
        noSmite["participants"][1]["spell2Id"] = 14;
        auto nm = parseMatch(convertLcuGameToV5(noSmite, nullptr).dump());
        CHECK(nm.has_value());
        if (nm) {
            CHECK_EQ(nm->byId(2)->position, std::string("JUNGLE"));   // 110 camps
            CHECK_EQ(nm->byId(1)->position, std::string("TOP"));      // 0 camps
        }
    }

    // --- ipc frame roundtrip -----------------------------------------------
    {
        HANDLE rd = nullptr, wr = nullptr;
        CHECK(CreatePipe(&rd, &wr, nullptr, 0));
        std::string payload = "{\"v\":1,\"type\":\"state\",\"state\":\"InGame\"}";
        CHECK(ipc::writeFrame(wr, payload));
        std::string got;
        CHECK(ipc::readFrame(rd, got));
        CHECK_EQ(got, payload);
        // Oversized frame is rejected before writing.
        std::string big(ipc::kMaxMessage + 1, 'x');
        CHECK(!ipc::writeFrame(wr, big));
        CloseHandle(rd);
        CloseHandle(wr);
    }

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
