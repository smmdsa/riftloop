#include "core/missions.h"
#include "core/detectors.h"
#include "core/util.h"

#include <algorithm>
#include <map>
#include <set>

namespace rl {

namespace {

double confidenceWeight(const std::string& c) {
    if (c == "alta") return 1.0;
    if (c == "media") return 0.75;
    return 0.5;
}

struct Template { const char* name, *hypothesis, *metric, *applies, *notApplies; };

const std::map<std::string, Template> kTemplates = {
    {"D01", {"Sobrevivir la fase de lineas",
             "Si reduces las muertes antes del minuto 8, llegas al mid game con recursos para decidir.",
             "Maximo 1 muerte antes del minuto 8 por partida.",
             "Partidas normales y ranked en tu rol principal.",
             "Remakes; partidas donde el rival hace dive con ventaja clara de jungla."}},
    {"D02", {"Llegar vivo al objetivo",
             "Si llegas con vida a la ventana del objetivo, tu equipo puede disputarlo 5v5.",
             "No morir entre 90 y 20 segundos antes de un objetivo disputable.",
             "Objetivos elite despues del minuto 8 cuando la pelea es disputable.",
             "Objetivos cedidos deliberadamente o jugada cross-map de mayor valor."}},
    {"D03", {"Respetar el umbral de recall",
             "Si mueres con menos oro encima, cada muerte cuesta menos progreso.",
             "Morir con menos de 1250 de oro sin gastar (medido por frame).",
             "Muertes despues del minuto 8.",
             "Muertes defendiendo una estructura clave con el equipo."}},
    {"D04", {"Comprar por umbral, no por costumbre",
             "Si gastas el oro en cuanto cruza tu umbral, tus picos de poder llegan antes.",
             "Ningun frame despues del minuto 12 con mas de 2200 de oro ocioso.",
             "Mid game en cualquier rol.",
             "Acumulacion deliberada para un objeto clave de mas de 3000."}},
    {"D05", {"Convertir cada kill",
             "Si cada kill temprana produce placa, oleada o compra, la ventaja se vuelve permanente.",
             "Tras una kill propia antes del 20, obtener placa/objetivo/compra en 75 segundos.",
             "Kills en las que participas antes del minuto 20.",
             "Kills en peleas defensivas bajo tu torre con oleada rival gigante."}},
    {"D06", {"Sostener el farmeo en mid game",
             "Si mantienes el CS despues de lineas, financias tus objetos sin depender de kills.",
             "CS/min despues del minuto 14 de al menos el 55 % del CS/min de lineas.",
             "Roles TOP, MIDDLE y BOTTOM en partidas de 22+ minutos.",
             "Partidas que terminan antes del 22; roles JUNGLE y UTILITY."}},
    {"D07", {"Vision antes del objetivo",
             "Si la vision llega 60-120 s antes del objetivo, tu equipo elige la pelea.",
             "Colocar al menos un ward en los 120 s previos a cada objetivo elite.",
             "Roles JUNGLE y UTILITY, objetivos despues del minuto 8.",
             "Objetivos regalados sin disputa o con el equipo en desventaja clara."}},
    {"D08", {"Contar antes de pelear",
             "Si no aceptas peleas en desventaja numerica, dejas de regalar tempo.",
             "Cero muertes con 3+ rivales involucrados sin aliados cerca.",
             "Mid y late game (despues del minuto 10).",
             "Sacrificios deliberados para salvar un objetivo mayor."}},
    {"D09", {"Adaptar la compra a la curacion",
             "Si compras antiheal cuando la amenaza existe, tus peleas largas cambian de resultado.",
             "Con 2+ curadores enfrente, tener el componente antiheal antes del minuto 22.",
             "Partidas contra composiciones con curacion fuerte.",
             "Composiciones sin curacion relevante."}},
    {"D10", {"Canjear la ventaja por mapa",
             "Si con +2500 de oro el equipo fija un objetivo, la ventaja se vuelve torres y mapa.",
             "Con ventaja de equipo de +2500, obtener un objetivo antes de que la ventaja caiga.",
             "Cuando tu equipo va claramente delante.",
             "Partidas siempre parejas o siempre por detras (metrica de equipo)."}},
};

} // namespace

Mission missionTemplate(const std::string& detectorId) {
    Mission m;
    m.detectorId = detectorId;
    auto it = kTemplates.find(detectorId);
    if (it != kTemplates.end()) {
        m.name = it->second.name;
        m.hypothesis = it->second.hypothesis;
        m.metric = it->second.metric;
        m.appliesWhen = it->second.applies;
        m.doesNotApply = it->second.notApplies;
    } else {
        m.name = "Mision " + detectorId;
    }
    m.blockSize = 4;
    m.targetSuccesses = 3;
    m.createdAt = util::nowIso();
    return m;
}

std::optional<Mission> suggestMission(Db& db) {
    // Aggregate the last 20 analyzed matches (PRD RF-POST-004).
    auto rows = db.listMatches(20);
    struct Agg { int matchesFired = 0; double sevSum = 0, confSum = 0; int opps = 0, fails = 0; };
    std::map<std::string, Agg> agg;
    int analyzed = 0;
    for (auto& r : rows) {
        auto a = db.loadAnalysis(r.matchId);
        if (!a) continue;
        ++analyzed;
        std::set<std::string> seen;
        for (auto& f : a->findings) {
            if (f.opportunities == 0) continue;
            Agg& x = agg[f.detectorId];
            x.opps += f.opportunities;
            x.fails += f.failures;
            x.sevSum += f.severity;
            x.confSum += confidenceWeight(f.confidence);
            if (f.failures > 0 && seen.insert(f.detectorId).second) ++x.matchesFired;
        }
    }
    if (analyzed == 0) return std::nullopt;

    std::string activeDetector;
    if (auto act = db.activeMission()) activeDetector = act->detectorId;

    std::string best;
    double bestScore = 0;
    for (auto& [id, x] : agg) {
        if (id == activeDetector) continue;
        if (x.opps < 3) continue;              // not enough opportunities (PRD 12.1)
        double recurrence = (double)x.matchesFired / analyzed;
        double failRate   = (double)x.fails / x.opps;
        double n          = (double)std::max(1, x.matchesFired);
        double score = recurrence * failRate * (x.sevSum / n) * (x.confSum / n) *
                       detectorControllability(id);
        if (score > bestScore) { bestScore = score; best = id; }
    }
    if (best.empty() || bestScore < 0.02) return std::nullopt;

    Mission m = missionTemplate(best);
    m.dbId = db.insertMission(m);
    return m;
}

void activateMission(Db& db, int64_t missionId) {
    for (auto& m : db.listMissions()) {
        if (m.dbId == missionId) {
            m.status = MissionStatus::Active;
            db.updateMission(m);
            db.recordActivity(util::todayLocal(), "mission_accepted");
            db.audit("mission_accepted", "{\"mission\":" + std::to_string(missionId) + "}");
        } else if (m.status == MissionStatus::Active) {
            m.status = MissionStatus::Discarded;   // one primary mission (PRD 3.4)
            db.updateMission(m);
        }
    }
}

void trackMatchForMission(Db& db, const AnalysisResult& res) {
    auto act = db.activeMission();
    if (!act) return;

    // Idempotence: skip when this match already produced opportunities.
    for (auto& o : db.opportunitiesFor(act->dbId))
        if (o.matchId == res.matchId) return;

    const Finding* f = nullptr;
    for (auto& x : res.findings)
        if (x.detectorId == act->detectorId) { f = &x; break; }

    Opportunity o;
    o.missionId = act->dbId;
    o.matchId = res.matchId;
    if (!f || f->opportunities == 0) {
        o.valid = false;
        o.note = "sin oportunidades validas en esta partida";
        db.insertOpportunity(o);
    } else {
        // Record each opportunity; successes are opportunities minus failures.
        for (int i = 0; i < f->opportunities; ++i) {
            Opportunity oi = o;
            oi.valid = true;
            oi.success = i >= f->failures;
            db.insertOpportunity(oi);
        }
        db.recordActivity(util::todayLocal(), "match_with_mission");
    }

    // Evaluate the block (PRD 12.3).
    auto prog = activeMissionProgress(db);
    if (!prog) return;
    if (prog->gamesTracked < prog->mission.blockSize) return;

    Mission m = prog->mission;
    m.status = MissionStatus::Evaluated;
    if (prog->validOpportunities < 3) {
        m.result = MissionResult::NotEnoughData;
    } else {
        double rate = (double)prog->successes / prog->validOpportunities;
        m.result = rate >= 0.75 ? MissionResult::Improved
                 : rate >= 0.5  ? MissionResult::Partial
                                : MissionResult::NoChange;
    }
    db.updateMission(m);
    db.audit("mission_evaluated", "{\"mission\":" + std::to_string(m.dbId) +
                                  ",\"result\":\"" + toString(m.result) + "\"}");

    // Skill tree update (PRD 9.15) + healthy XP (PRD 9.16).
    std::string domain = detectorDomain(m.detectorId);
    SkillNode node;
    node.domain = domain;
    for (auto& s : db.loadSkills())
        if (s.domain == domain) node = s;
    node.opportunitiesSeen += prog->validOpportunities;
    switch (m.result) {
        case MissionResult::Improved:
            node.state = node.state == SkillState::Consistent ? SkillState::Mastered
                                                              : SkillState::Consistent;
            node.confidence = "media";
            db.addXp(50, "mision consolidada: " + m.name);
            break;
        case MissionResult::Partial:
            node.state = SkillState::Practicing;
            node.confidence = "media";
            db.addXp(25, "progreso parcial: " + m.name);
            break;
        case MissionResult::NoChange:
            node.state = SkillState::Introduced;
            db.addXp(10, "bloque completado: " + m.name);
            break;
        default:
            db.addXp(5, "bloque sin datos suficientes");
            break;
    }
    db.saveSkill(node);
}

std::optional<MissionProgress> activeMissionProgress(Db& db) {
    auto act = db.activeMission();
    if (!act) return std::nullopt;
    MissionProgress p;
    p.mission = *act;
    std::set<std::string> games;
    for (auto& o : db.opportunitiesFor(act->dbId)) {
        games.insert(o.matchId);
        if (o.valid) {
            ++p.validOpportunities;
            if (o.success) ++p.successes;
        }
    }
    p.gamesTracked = (int)games.size();
    return p;
}

} // namespace rl
