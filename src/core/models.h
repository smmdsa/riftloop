// RiftLoop core data model. PRD sections 9, 11, 12, 30.
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rl {

// ---------------------------------------------------------------- match data

struct Participant {
    int         participantId = 0;   // 1..10
    std::string puuid;
    std::string riotId;              // gameName#tagLine
    std::string championName;
    int         championId = 0;
    int         teamId     = 0;      // 100 blue, 200 red
    std::string position;            // TOP JUNGLE MIDDLE BOTTOM UTILITY
    bool        win     = false;
    int         kills   = 0;
    int         deaths  = 0;
    int         assists = 0;
    int         goldEarned = 0;
    int         totalCs    = 0;      // minions + jungle
    int         champLevel = 0;
    std::array<int, 2>  summonerSpells{0, 0};
    std::vector<int>    finalItems;  // item0..item5
};

struct MatchSummary {
    std::string matchId;
    std::string gameVersion;         // "16.17.702.1234"
    std::string patch;               // "16.17"
    std::string queue;               // "RANKED_SOLO_5x5", "NORMAL", ...
    int64_t     gameCreationMs = 0;
    int         gameDurationSec = 0;
    bool        remake = false;
    std::vector<Participant> participants;

    const Participant* byPuuid(const std::string& p) const {
        for (auto& x : participants) if (x.puuid == p) return &x;
        return nullptr;
    }
    const Participant* byId(int id) const {
        for (auto& x : participants) if (x.participantId == id) return &x;
        return nullptr;
    }
};

// ------------------------------------------------------------------ timeline

enum class TLType {
    ChampionKill, ItemPurchased, ItemSold, ItemUndo,
    WardPlaced, EliteMonsterKill, BuildingKill, Other
};

struct TLEvent {
    TLType      type = TLType::Other;
    int64_t     tsMs = 0;
    int         participantId = 0;   // actor (killer / buyer / ward placer)
    int         victimId = 0;        // ChampionKill
    std::vector<int> assistIds;      // ChampionKill
    int         itemId  = 0;         // Item*
    int         killerTeamId = 0;    // EliteMonsterKill / BuildingKill
    std::string monsterType;         // DRAGON, BARON_NASHOR, RIFTHERALD, ...
    std::string buildingType;        // TOWER_BUILDING, ...
    int         posX = -1, posY = -1;
};

struct TLFrame {
    int64_t tsMs = 0;
    // participantId -> value
    std::map<int, int> totalGold;
    std::map<int, int> currentGold;
    std::map<int, int> cs;           // minions + jungle
    std::map<int, int> level;
};

struct Timeline {
    std::string matchId;
    std::vector<TLFrame> frames;     // one per minute
    std::vector<TLEvent> events;
};

// -------------------------------------------------------------- player state

enum class PoolTier { Main, Comfort, Learning, DoNotRecommend };

struct PoolEntry {
    std::string champion;            // ddragon id, e.g. "Thresh"
    std::string role;                // TOP/JUNGLE/MIDDLE/BOTTOM/UTILITY
    PoolTier    tier = PoolTier::Comfort;
    int         declaredGames = 0;   // manual input for cold start (RF-ONB-003)
};

enum class AppMode { Escalar, Aprender };

struct Profile {
    std::string riotId;              // informative only; no account system
    std::string puuid;               // resolved from imported matches
    std::string region = "la2";
    AppMode     mode = AppMode::Escalar;
    std::vector<std::string> preferredRoles;   // ordered
    std::vector<PoolEntry>   pool;
    std::string language = "es";
};

// ------------------------------------------------------- findings & evidence

struct Evidence {                    // PRD section 30 contract
    std::string evidenceId;
    std::string matchId;
    int64_t     gameTimestampMs = 0;
    std::string source;              // "timeline"
    std::string observedFacts;       // facts only
    std::string inference;           // separated from facts
    std::string confidence;          // alta / media / baja
    std::string exclusionsChecked;
};

struct Finding {
    int64_t     dbId = 0;
    std::string detectorId;          // D01..D10
    std::string matchId;
    std::string title;
    std::string whyItMatters;
    std::string alternative;         // what to try next time
    std::string confidence;          // alta / media / baja
    double      severity = 0.0;      // 0..1
    int         opportunities = 0;   // valid opportunities observed
    int         failures = 0;        // opportunities not executed well
    std::vector<Evidence> evidence;
};

struct AnalysisResult {
    std::string matchId;
    std::string rulesetVersion;
    std::string strength;            // one observed strength
    std::vector<Finding> findings;   // ordered by priority
    std::string limitations;
};

// ------------------------------------------------------------------ missions

enum class MissionStatus { Suggested, Active, Evaluated, Discarded };
enum class MissionResult { None, Improved, Partial, NoChange, NotEnoughData, DetectorWrong };

struct Mission {
    int64_t     dbId = 0;
    std::string detectorId;
    std::string name;                // behavioral name
    std::string hypothesis;
    std::string metric;              // observable metric
    std::string appliesWhen;
    std::string doesNotApply;
    int         blockSize = 4;       // 3..5 games
    int         targetSuccesses = 3;
    MissionStatus status = MissionStatus::Suggested;
    MissionResult result = MissionResult::None;
    std::string createdAt;           // ISO date
};

struct Opportunity {
    int64_t     missionId = 0;
    std::string matchId;
    bool        valid   = true;
    bool        success = false;
    std::string note;
};

// ---------------------------------------------------------------- skill tree

enum class SkillState { NotEvaluated, Introduced, Practicing, Consistent, Mastered, NeedsRefresh };

struct SkillNode {
    std::string domain;              // "laning", "vision", ...
    SkillState  state = SkillState::NotEvaluated;
    std::string confidence = "baja";
    int         opportunitiesSeen = 0;
};

// ----------------------------------------------------------- recommendations

struct CompTraits {                  // PRD 11.3, simplified vector
    double physical = 0, magical = 0;
    double burst = 0, sustainedDps = 0;
    double frontline = 0, engage = 0, disengage = 0, peel = 0;
    double cc = 0, range = 0, waveclear = 0;
    double earlyPower = 0, scaling = 0;
    double healingShields = 0;       // enemy threat input for items
};

struct DraftContext {
    std::string role;                // user assigned role
    std::vector<std::string> allyChampions;   // known picks (without user)
    std::vector<std::string> enemyChampions;  // known picks
    std::vector<std::string> bans;
    std::vector<std::string> ownedOrPickable; // empty = unknown -> use pool
    std::string patch;
};

struct ChampCard {
    std::string champion;
    std::string label;               // "Mejor opción personal" | "Opción segura" | "Opción estratégica"
    std::vector<std::string> reasons;    // max 3
    std::string risk;
    std::string experience;          // user's experience summary
    std::string confidence;          // alta / media / baja
};

struct Top3 {
    std::vector<ChampCard> cards;    // <= 3
    std::string uncertaintyReason;
    std::string patch;
    bool        available = true;    // false => explanation why not
    std::string unavailableReason;
};

struct RunePage {
    std::string name;
    int primaryStyle = 0, subStyle = 0;
    std::vector<int> perks;          // 4 primary + 2 sub + 3 shards
    std::vector<std::string> reasons;    // max 3 non-obvious decisions
};

struct RunePlan {
    RunePage main;
    std::optional<RunePage> situational;
    std::string confidence;
};

struct SpellPlan {
    std::array<std::string, 2> spells;   // e.g. {"Flash","Teleport"}
    std::string reason;
};

struct ItemBranch {
    std::string label;               // "Contra curación"
    std::vector<int> items;
    std::string condition;           // "elige esto si..."
};

struct ItemPlan {
    std::vector<int> starting;
    std::vector<int> firstBack;      // components by gold threshold
    int firstBackGold = 900;
    std::vector<int> core;           // core 1 + core 2
    std::vector<ItemBranch> boots;   // options with conditions
    std::vector<ItemBranch> branches;    // situational, <= 3 shown in overlay
    std::string confidence;
    std::string datasetNote;         // where numbers come from (honesty, RF-ITEM-003)
};

struct QuizQuestion {
    std::string conceptTag;             // e.g. "amenaza-principal"
    std::string text;
    std::vector<std::string> options;    // 2..4
    int         correctIndex = 0;
    std::string explanation;         // one line shown on wrong answer
};

// ------------------------------------------------------------------- states

enum class GameState {
    NoClient, ClientOpen, Lobby, Queue, ReadyCheck,
    ChampSelect, Loading, InGame, PostGame, Processing
};

const char* toString(GameState s);
const char* toString(SkillState s);
const char* toString(MissionStatus s);
const char* toString(MissionResult r);

} // namespace rl
