#include "core/detectors.h"
#include "core/util.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace rl {

namespace {

constexpr int64_t kMin = 60'000;

// Anti-heal items checked against the installed patch before use (D09).
const std::vector<int> kAntiHealItems = {3123, 3033, 3165, 3011, 3075, 8020, 3222, 6609};

struct Ctx {
    const DetectorInput& in;
    const Participant*   user;
    int                  userTeam;

    std::vector<const TLEvent*> userDeaths;   // ChampionKill where victim == user
    std::vector<const TLEvent*> eliteKills;
    std::vector<const TLEvent*> teamObjectives;   // buildings + elites by user's team

    explicit Ctx(const DetectorInput& i) : in(i) {
        user = i.match.byId(i.userId);
        userTeam = user ? user->teamId : 0;
        for (auto& e : i.timeline.events) {
            if (e.type == TLType::ChampionKill && e.victimId == i.userId)
                userDeaths.push_back(&e);
            if (e.type == TLType::EliteMonsterKill)
                eliteKills.push_back(&e);
            if ((e.type == TLType::EliteMonsterKill || e.type == TLType::BuildingKill) &&
                e.killerTeamId == userTeam)
                teamObjectives.push_back(&e);
        }
    }

    const TLFrame* frameAt(int64_t tsMs) const {
        const TLFrame* best = nullptr;
        for (auto& f : in.timeline.frames) {
            if (f.tsMs <= tsMs) best = &f;
            else break;
        }
        return best ? best : (in.timeline.frames.empty() ? nullptr : &in.timeline.frames.front());
    }
    int currentGoldAt(int64_t tsMs) const {
        const TLFrame* f = frameAt(tsMs);
        if (!f) return -1;
        auto it = f->currentGold.find(in.userId);
        return it == f->currentGold.end() ? -1 : it->second;
    }
    int teamGoldDiffAt(const TLFrame& f) const {
        int mine = 0, theirs = 0;
        for (auto& [pid, g] : f.totalGold) {
            const Participant* p = in.match.byId(pid);
            if (!p) continue;
            (p->teamId == userTeam ? mine : theirs) += g;
        }
        return mine - theirs;
    }
    // Purchase clusters: shop visits (purchases separated by < 25 s).
    struct Visit { int64_t tsMs; int spend; };
    std::vector<Visit> shopVisits() const {
        std::vector<Visit> out;
        for (auto& e : in.timeline.events) {
            if (e.type != TLType::ItemPurchased || e.participantId != in.userId) continue;
            int cost = 0;
            if (in.dd) {
                if (const ItemInfo* it = in.dd->item(e.itemId)) cost = it->totalGold;
            }
            if (!out.empty() && e.tsMs - out.back().tsMs < 25'000)
                out.back().spend += cost;
            else
                out.push_back({e.tsMs, cost});
        }
        return out;
    }
    Evidence makeEvidence(int64_t tsMs, std::string facts, std::string inference,
                          std::string confidence, std::string exclusions) const {
        Evidence ev;
        ev.evidenceId = in.match.matchId + "-" + std::to_string(tsMs);
        ev.matchId = in.match.matchId;
        ev.gameTimestampMs = tsMs;
        ev.source = "timeline";
        ev.observedFacts = std::move(facts);
        ev.inference = std::move(inference);
        ev.confidence = std::move(confidence);
        ev.exclusionsChecked = std::move(exclusions);
        return ev;
    }
};

Finding baseFinding(const Ctx& c, const char* id, std::string title, std::string why,
                    std::string alt, std::string conf, double sev) {
    Finding f;
    f.detectorId = id;
    f.matchId = c.in.match.matchId;
    f.title = std::move(title);
    f.whyItMatters = std::move(why);
    f.alternative = std::move(alt);
    f.confidence = std::move(conf);
    f.severity = sev;
    return f;
}

// --- D01: avoidable early deaths --------------------------------------------
void d01(const Ctx& c, std::vector<Finding>& out) {
    Finding f = baseFinding(c, "D01", "Muertes tempranas evitables",
        "Morir antes del minuto 8 cede oro, experiencia y presion de linea.",
        "Juega la ventana 0-8 con una regla simple: sin vision del jungla rival, no cruces la mitad de la oleada.",
        "media", 0.9);
    f.opportunities = 1;   // one early-game window per match
    int early = 0;
    for (auto* d : c.userDeaths) {
        if (d->tsMs > 8 * kMin) continue;
        ++early;
        int enemies = 1 + (int)d->assistIds.size();
        f.evidence.push_back(c.makeEvidence(d->tsMs,
            "Muerte al " + util::formatGameClock(d->tsMs) + " con " +
                std::to_string(enemies) + " rival(es) involucrado(s).",
            enemies >= 2 ? "Patron compatible con gank o collapse; la ventana previa era el momento de retroceder."
                         : "Muerte 1v1 temprana; revisa el intercambio previo en el clip.",
            "media",
            "remake excluido; no se conoce el estado de la oleada desde el timeline"));
    }
    if (early >= 2) f.failures = 1;
    if (early > 0) out.push_back(std::move(f));
    else if (f.opportunities > 0) out.push_back(std::move(f));   // success case counts too
}

// --- D02: death before objective window -------------------------------------
void d02(const Ctx& c, std::vector<Finding>& out) {
    Finding f = baseFinding(c, "D02", "Muerte antes de objetivo",
        "Morir en los 90 s previos a un objetivo regala la pelea o el objetivo sin jugarla.",
        "Prepara el objetivo 60-90 s antes: recall, compra y posicion con tu equipo.",
        "media", 0.85);
    for (auto* ek : c.eliteKills) {
        if (ek->tsMs < 8 * kMin) continue;
        ++f.opportunities;
        for (auto* d : c.userDeaths) {
            if (d->tsMs >= ek->tsMs - 90'000 && d->tsMs < ek->tsMs) {
                bool enemyTook = ek->killerTeamId != c.userTeam;
                ++f.failures;
                f.evidence.push_back(c.makeEvidence(d->tsMs,
                    "Muerte al " + util::formatGameClock(d->tsMs) + "; " + ek->monsterType +
                        (enemyTook ? " cayo para el rival al " : " se tomo al ") +
                        util::formatGameClock(ek->tsMs) + ".",
                    "La muerte dentro de la ventana previa redujo la capacidad de disputar el objetivo.",
                    "media",
                    "no se confirma desde el timeline si el objetivo se cedia deliberadamente"));
                break;
            }
        }
    }
    if (f.opportunities > 0) out.push_back(std::move(f));
}

// --- D03: unspent gold at death ---------------------------------------------
void d03(const Ctx& c, std::vector<Finding>& out) {
    constexpr int kThreshold = 1250;
    Finding f = baseFinding(c, "D03", "Oro sin gastar al morir",
        "El oro en el bolsillo no pelea. Morir con mas de 1250 de oro es perder un objeto de diferencia.",
        "Define un umbral personal de recall (~1250) y respetalo cuando la oleada este empujada.",
        "alta", 0.7);
    for (auto* d : c.userDeaths) {
        if (d->tsMs < 8 * kMin) continue;
        ++f.opportunities;
        int gold = c.currentGoldAt(d->tsMs);
        if (gold >= kThreshold) {
            ++f.failures;
            f.evidence.push_back(c.makeEvidence(d->tsMs,
                "Muerte al " + util::formatGameClock(d->tsMs) + " con ~" + std::to_string(gold) +
                    " de oro sin gastar (frame mas cercano).",
                "Con una compra previa, ese intercambio se juega con estadisticas superiores.",
                "alta",
                "el oro proviene del frame por minuto mas cercano; margen de +-1 min"));
        }
    }
    if (f.opportunities > 0) out.push_back(std::move(f));
}

// --- D04: sustained idle gold ------------------------------------------------
void d04(const Ctx& c, std::vector<Finding>& out) {
    Finding f = baseFinding(c, "D04", "Oro ocioso sostenido",
        "Mantener >2200 de oro durante minutos retrasa tus subidas de poder.",
        "Planifica la primera vuelta y los recalls por umbral de oro, no por costumbre.",
        "media", 0.55);
    for (auto& fr : c.in.timeline.frames) {
        if (fr.tsMs < 12 * kMin) continue;
        ++f.opportunities;
        auto it = fr.currentGold.find(c.in.userId);
        if (it != fr.currentGold.end() && it->second >= 2200) {
            ++f.failures;
            if (f.evidence.size() < 3)
                f.evidence.push_back(c.makeEvidence(fr.tsMs,
                    "Minuto " + util::formatGameClock(fr.tsMs) + ": ~" +
                        std::to_string(it->second) + " de oro sin gastar.",
                    "Habia margen para recall y compra sin perder una jugada visible en el timeline.",
                    "media",
                    "no se descarta que estuvieras defendiendo una jugada; revisa el contexto"));
        }
    }
    if (f.opportunities > 0) out.push_back(std::move(f));
}

// --- D05: kills without conversion ------------------------------------------
void d05(const Ctx& c, std::vector<Finding>& out) {
    Finding f = baseFinding(c, "D05", "Kills sin conversion",
        "Una kill vale por lo que compras con ella: placas, oleada, vision u objetivo.",
        "Despues de cada kill temprana aplica un checklist: empujar, placa, recall o rotar.",
        "media", 0.6);
    auto visits = c.shopVisits();
    for (auto& e : c.in.timeline.events) {
        if (e.type != TLType::ChampionKill) continue;
        bool involved = e.participantId == c.in.userId ||
            std::find(e.assistIds.begin(), e.assistIds.end(), c.in.userId) != e.assistIds.end();
        if (!involved || e.tsMs > 20 * kMin) continue;
        ++f.opportunities;
        bool converted = false;
        for (auto* obj : c.teamObjectives)
            if (obj->tsMs > e.tsMs && obj->tsMs <= e.tsMs + 75'000) { converted = true; break; }
        if (!converted)
            for (auto& v : visits)
                if (v.tsMs > e.tsMs && v.tsMs <= e.tsMs + 75'000 && v.spend >= 700) {
                    converted = true; break;
                }
        if (!converted) {
            ++f.failures;
            if (f.evidence.size() < 3)
                f.evidence.push_back(c.makeEvidence(e.tsMs,
                    "Kill al " + util::formatGameClock(e.tsMs) +
                        " sin placa, objetivo ni compra relevante en los 75 s siguientes.",
                    "La ventaja momentanea no se transformo en una ventaja permanente.",
                    "media",
                    "el timeline no registra el estado de la oleada ni la vida restante"));
        }
    }
    if (f.opportunities > 0) out.push_back(std::move(f));
}

// --- D06: CS drop after lane phase ------------------------------------------
void d06(const Ctx& c, std::vector<Finding>& out) {
    if (!c.user) return;
    if (c.user->position == "UTILITY" || c.user->position == "JUNGLE") return;
    if (c.in.match.gameDurationSec < 22 * 60) return;

    auto csAt = [&](int64_t ts) {
        const TLFrame* fr = c.frameAt(ts);
        if (!fr) return 0;
        auto it = fr->cs.find(c.in.userId);
        return it == fr->cs.end() ? 0 : it->second;
    };
    double early = (csAt(14 * kMin) - csAt(4 * kMin)) / 10.0;    // cs/min 4-14
    int64_t endTs = std::min<int64_t>((int64_t)c.in.match.gameDurationSec * 1000, 26 * kMin);
    double mid = (csAt(endTs) - csAt(14 * kMin)) / ((endTs - 14 * kMin) / (double)kMin);
    if (early < 3.0) return;   // lane phase data too weak to judge

    Finding f = baseFinding(c, "D06", "Caida de CS despues de linea",
        "El oro de oleadas no depende del rival. Perderlo en mid game frena tus objetos.",
        "Entre jugadas, vuelve a una oleada segura antes de caminar por el rio sin plan.",
        "media", 0.5);
    f.opportunities = 1;
    if (mid < early * 0.55) {
        f.failures = 1;
        char buf[160];
        snprintf(buf, sizeof buf,
                 "CS/min 4-14: %.1f. CS/min 14-%lld: %.1f (caida > 45 %%).",
                 early, endTs / kMin, mid);
        f.evidence.push_back(c.makeEvidence(14 * kMin, buf,
            "Parte de la caida suele ser farmeo abandonado, no solo teamfights.",
            "media", "roles UTILITY y JUNGLE excluidos; partidas < 22 min excluidas"));
    }
    out.push_back(std::move(f));
}

// --- D07: late vision before objectives (jungle/support) --------------------
void d07(const Ctx& c, std::vector<Finding>& out) {
    if (!c.user) return;
    if (c.user->position != "JUNGLE" && c.user->position != "UTILITY") return;

    std::vector<int64_t> wards;
    for (auto& e : c.in.timeline.events)
        if (e.type == TLType::WardPlaced && e.participantId == c.in.userId)
            wards.push_back(e.tsMs);

    Finding f = baseFinding(c, "D07", "Vision tardia de objetivo",
        "Sin vision previa, la pelea por el objetivo se juega a ciegas.",
        "Coloca vision en el cuadrante del objetivo 60-120 s antes de su ventana.",
        "baja", 0.6);
    for (auto* ek : c.eliteKills) {
        if (ek->tsMs < 8 * kMin) continue;
        ++f.opportunities;
        bool covered = std::any_of(wards.begin(), wards.end(), [&](int64_t w) {
            return w >= ek->tsMs - 120'000 && w < ek->tsMs;
        });
        if (!covered) {
            ++f.failures;
            if (f.evidence.size() < 3)
                f.evidence.push_back(c.makeEvidence(ek->tsMs,
                    std::string(ek->monsterType) + " al " + util::formatGameClock(ek->tsMs) +
                        " sin ward tuyo en los 120 s previos.",
                    "La preparacion de vision llego tarde o no ocurrio.",
                    "baja",
                    "no se comprueba la posicion del ward, solo el momento"));
        }
    }
    if (f.opportunities > 0) out.push_back(std::move(f));
}

// --- D08: outnumbered deaths -------------------------------------------------
void d08(const Ctx& c, std::vector<Finding>& out) {
    Finding f = baseFinding(c, "D08", "Peleas en desventaja numerica",
        "Un 1v3 casi nunca es una pelea: es un regalo de tempo al rival.",
        "Antes de pelear, cuenta: si hay mas de un rival visible de diferencia, retrocede.",
        "baja", 0.75);
    for (auto* d : c.userDeaths) {
        if (d->tsMs < 10 * kMin) continue;
        ++f.opportunities;
        int enemies = 1 + (int)d->assistIds.size();
        // Ally presence proxy: allied kills or deaths within 12 s of this death.
        bool alliesNearby = false;
        for (auto& e : c.in.timeline.events) {
            if (e.type != TLType::ChampionKill || std::llabs(e.tsMs - d->tsMs) > 12'000) continue;
            if (&e == d) continue;
            const Participant* victim = c.in.match.byId(e.victimId);
            const Participant* killer = c.in.match.byId(e.participantId);
            if ((victim && victim->teamId == c.userTeam && e.victimId != c.in.userId) ||
                (killer && killer->teamId == c.userTeam))
                { alliesNearby = true; break; }
        }
        if (enemies >= 3 && !alliesNearby) {
            ++f.failures;
            if (f.evidence.size() < 3)
                f.evidence.push_back(c.makeEvidence(d->tsMs,
                    "Muerte al " + util::formatGameClock(d->tsMs) + " con " +
                        std::to_string(enemies) + " rivales involucrados y sin actividad aliada cercana.",
                    "Patron compatible con una pelea aceptada en desventaja numerica.",
                    "baja",
                    "la presencia aliada se estima por eventos, no por posiciones"));
        }
    }
    if (f.opportunities > 0) out.push_back(std::move(f));
}

// --- D09: late anti-heal adaptation -----------------------------------------
void d09(const Ctx& c, std::vector<Finding>& out) {
    if (!c.in.dd) return;
    const Ddragon& dd = *c.in.dd;

    std::vector<std::string> enemyChamps;
    int healers = 0;
    for (auto& p : c.in.match.participants) {
        if (p.teamId == c.userTeam) continue;
        enemyChamps.push_back(p.championName);
        if (dd.championHealsHeavily(p.championName)) ++healers;
    }
    if (healers < 2) return;                       // opportunity does not exist
    if (c.in.match.gameDurationSec < 22 * 60) return;

    Finding f = baseFinding(c, "D09", "Adaptacion tardia contra curacion",
        "Con 2+ campeones de curacion fuerte enfrente, las heridas graves valen mas que un objeto de daño extra.",
        "Cuando identifiques 2 fuentes de curacion, compra el componente antiheal en la siguiente vuelta.",
        "media", 0.65);
    f.opportunities = 1;

    int64_t boughtAt = -1;
    for (auto& e : c.in.timeline.events) {
        if (e.type != TLType::ItemPurchased || e.participantId != c.in.userId) continue;
        for (int id : kAntiHealItems)
            if (e.itemId == id && dd.item(id)) { boughtAt = e.tsMs; break; }
        if (boughtAt >= 0) break;
    }
    if (boughtAt < 0 || boughtAt > 22 * kMin) {
        f.failures = 1;
        f.evidence.push_back(c.makeEvidence(std::min<int64_t>(22 * kMin, (int64_t)c.in.match.gameDurationSec * 1000),
            std::to_string(healers) + " campeones rivales con curacion fuerte; " +
                (boughtAt < 0 ? "no compraste antiheal en toda la partida."
                              : "el antiheal llego al " + util::formatGameClock(boughtAt) + "."),
            "La amenaza estaba visible desde champion select; la adaptacion llego tarde.",
            "media",
            "lista de curadores curada por version; puede omitir amenazas nuevas"));
    }
    out.push_back(std::move(f));
}

// --- D10: lead not converted -------------------------------------------------
void d10(const Ctx& c, std::vector<Finding>& out) {
    Finding f = baseFinding(c, "D10", "Ventaja de oro no convertida",
        "Una ventaja de equipo caduca sola: el lado que va delante debe canjearla por mapa.",
        "Con +2500 de oro de equipo, fija el proximo objetivo con tu equipo y prepara la jugada.",
        "media", 0.7);
    const auto& frames = c.in.timeline.frames;
    for (size_t i = 0; i < frames.size(); ++i) {
        int lead = c.teamGoldDiffAt(frames[i]);
        if (lead < 2500) continue;
        ++f.opportunities;
        // Look ahead up to 8 minutes: lead collapses without a team objective.
        bool collapsed = false;
        int64_t collapseTs = 0;
        for (size_t j = i + 1; j < frames.size() && frames[j].tsMs <= frames[i].tsMs + 8 * kMin; ++j) {
            if (c.teamGoldDiffAt(frames[j]) < 500) {
                collapsed = true;
                collapseTs = frames[j].tsMs;
                break;
            }
        }
        if (collapsed) {
            bool tookSomething = std::any_of(c.teamObjectives.begin(), c.teamObjectives.end(),
                [&](const TLEvent* o) { return o->tsMs >= frames[i].tsMs && o->tsMs <= collapseTs; });
            if (!tookSomething) {
                ++f.failures;
                if (f.evidence.size() < 2)
                    f.evidence.push_back(c.makeEvidence(frames[i].tsMs,
                        "Ventaja de +" + std::to_string(c.teamGoldDiffAt(frames[i])) +
                            " al " + util::formatGameClock(frames[i].tsMs) +
                            " evaporada al " + util::formatGameClock(collapseTs) +
                            " sin objetivo tomado en medio.",
                        "La ventana de presion no se canjeo por torres ni objetivos.",
                        "media",
                        "metrica de equipo; tu control individual es parcial"));
            }
        }
        // Skip forward past this lead episode.
        while (i + 1 < frames.size() && c.teamGoldDiffAt(frames[i + 1]) >= 2500) ++i;
    }
    if (f.opportunities > 0) out.push_back(std::move(f));
}

} // namespace

std::vector<Finding> runDetectors(const DetectorInput& in) {
    std::vector<Finding> out;
    if (in.match.remake || in.timeline.frames.empty()) return out;
    Ctx c(in);
    if (!c.user) return out;
    d01(c, out); d02(c, out); d03(c, out); d04(c, out); d05(c, out);
    d06(c, out); d07(c, out); d08(c, out); d09(c, out); d10(c, out);
    return out;
}

std::string detectStrength(const DetectorInput& in) {
    Ctx c(in);
    if (!c.user) return {};
    int minutes = std::max(1, in.match.gameDurationSec / 60);

    if (c.user->deaths <= 2 && minutes >= 22)
        return "Disciplina de muertes: " + std::to_string(c.user->deaths) +
               " muertes en " + std::to_string(minutes) + " minutos. Conserva ese criterio de riesgo.";
    double csPerMin = c.user->totalCs / (double)minutes;
    if (csPerMin >= 7.0 && c.user->position != "UTILITY" && c.user->position != "JUNGLE")
        return "Farmeo consistente: " + std::to_string((int)(csPerMin * 10) / 10) +
               " CS/min sostenido. Es una base economica solida.";
    int kp = c.user->kills + c.user->assists;
    if (kp >= 10)
        return "Participacion en jugadas: " + std::to_string(kp) +
               " kills+asistencias. Estas presente cuando el mapa se mueve.";
    if (c.user->win)
        return "Cierre de partida: el resultado acompanio a las decisiones de mid-late game.";
    return "Completaste la partida entera; cada timeline completo mejora el diagnostico siguiente.";
}

std::string detectorDomain(const std::string& id) {
    if (id == "D01" || id == "D08") return "posicionamiento";
    if (id == "D02" || id == "D07") return "objetivos";
    if (id == "D03" || id == "D04") return "recalls-economia";
    if (id == "D05" || id == "D10") return "conversion-ventajas";
    if (id == "D06") return "wave-management";
    if (id == "D09") return "itemizacion";
    return "general";
}

double detectorControllability(const std::string& id) {
    if (id == "D03" || id == "D04" || id == "D09") return 1.0;   // fully user-controlled
    if (id == "D01" || id == "D02" || id == "D06" || id == "D07") return 0.8;
    if (id == "D05") return 0.6;
    return 0.4;                                                  // D08, D10: team context
}

} // namespace rl
