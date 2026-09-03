// SQLite persistence. Single local profile, no accounts (goal: local MVP1).
#pragma once
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
    std::vector<MatchRow> listMatches(int limit = 50);
    std::vector<std::string> pendingAnalysis();
    std::string matchJson(const std::string& matchId);
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
