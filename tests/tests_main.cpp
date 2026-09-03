// Core test suite. Builds synthetic match/timeline fixtures and checks the
// detector, mission, recommendation and planner rules end to end.
#include "core/analysis.h"
#include "core/config.h"
#include "core/contracts.h"
#include "core/db.h"
#include "core/ddragon.h"
#include "core/detectors.h"
#include "core/ingest.h"
#include "core/ipc.h"
#include "core/lcu_history.h"
#include "core/missions.h"
#include "core/patchimpact.h"
#include "core/planner.h"
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
        ctx.allyChampions = {"Garen", "LeeSin", "Jinx", "Thresh"};
        ctx.enemyChampions = {"Aatrox", "Soraka", "Zed", "Caitlyn", "Leona"};
        ctx.bans = {"Orianna"};
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
        RunePlan rp = planRunes(dd, pi);
        CHECK(rp.main.perks.size() == 9);
        // All non-shard perks exist in the installed patch (RF-RUN-002).
        for (int i = 0; i < 6; ++i) CHECK(dd.runeExists(rp.main.perks[i]));
        CHECK((int)rp.main.reasons.size() <= 3);

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
                          {"item4", 0}, {"item5", 0}};
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
