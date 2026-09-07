#include "core/recommend.h"
#include "core/ingest.h"
#include "core/util.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace rl {

namespace {

struct Candidate {
    PoolEntry   entry;
    double      prof = 0;            // 0..1
    double      fit = 0;             // 0..1 contribution to comp needs
    int         recentGames = 0;
    std::vector<std::string> fitReasons;
};

// What the ally comp lacks, given the enemy comp (simplified PRD 11.3).
struct Needs {
    double frontline = 0, engage = 0, peel = 0, magicDamage = 0, physDamage = 0,
           disengage = 0, antiheal = 0;
    std::vector<std::pair<double, std::string>> ranked;   // weight, label
};

Needs computeNeeds(const Ddragon& dd, const DraftContext& ctx) {
    CompTraits ally = dd.teamTraits(ctx.allyChampions());
    CompTraits enemy = dd.teamTraits(ctx.enemyChampions());
    Needs n;
    n.frontline = std::max(0.0, 1.4 - ally.frontline);
    n.engage    = std::max(0.0, 1.0 - ally.engage);
    n.peel      = enemy.burst > 1.5 ? std::max(0.0, 1.0 - ally.peel) : 0.3;
    n.disengage = enemy.engage > 1.5 ? std::max(0.0, 1.0 - ally.disengage) : 0.2;
    // Damage mix: if the team leans one way, value the other (PRD 9.4 traits).
    double total = ally.physical + ally.magical + 0.01;
    n.magicDamage = ally.magical / total < 0.30 ? 1.0 : 0.2;
    n.physDamage  = ally.physical / total < 0.30 ? 1.0 : 0.2;
    n.antiheal    = enemy.healingShields >= 2 ? 0.6 : 0.0;

    n.ranked = {{n.frontline, "frontline"}, {n.engage, "engage"}, {n.peel, "peel"},
                {n.magicDamage, "daño magico"}, {n.physDamage, "daño fisico"},
                {n.disengage, "disengage"}};
    std::sort(n.ranked.rbegin(), n.ranked.rend());
    return n;
}

double fitScore(const Ddragon& dd, const std::string& champ, const Needs& n,
                std::vector<std::string>* reasons) {
    CompTraits t = dd.traits(champ);
    double score = 0, denom = 0;
    auto add = [&](double need, double have, const char* label) {
        denom += need;
        double part = need * std::min(1.0, have);
        score += part;
        if (reasons && need >= 0.8 && have >= 0.6)
            reasons->push_back(std::string("Aporta ") + label + " que a la composicion le falta");
    };
    add(n.frontline, t.frontline, "frontline");
    add(n.engage, t.engage, "engage");
    add(n.peel, t.peel, "peel");
    add(n.magicDamage, t.magical / 8.0, "daño magico");
    add(n.physDamage, t.physical / 8.0, "daño fisico");
    add(n.disengage, t.disengage, "disengage");
    return denom > 0 ? score / denom : 0.5;
}

std::string expLabel(int games) {
    if (games >= 20) return "Dominio alto (" + std::to_string(games) + " partidas registradas)";
    if (games >= 8)  return "Experiencia media (" + std::to_string(games) + " partidas)";
    if (games >= 1)  return "Experiencia baja (" + std::to_string(games) + " partidas)";
    return "Sin partidas registradas";
}

} // namespace

double proficiency(Db& db, const std::string& champion, const std::string& role) {
    // Effective games with temporal decay (PRD 11.2), from local match history.
    auto rows = db.listMatches(60);
    double eff = 0;
    int idx = 0;
    int wins = 0, games = 0;
    for (auto& r : rows) {
        if (r.userChampion == champion && (role.empty() || r.userRole == role || r.userRole.empty())) {
            eff += std::pow(0.97, idx);      // newer games weigh more
            ++games;
            if (r.userWin) ++wins;
        }
        ++idx;
    }
    // Declared experience is a prior, never certainty (RF-ONB-003).
    Profile p = db.loadProfile();
    for (auto& e : p.pool)
        if (e.champion == champion) eff += std::min(10, e.declaredGames) * 0.3;
    (void)wins;
    return std::min(1.0, eff / 15.0);
}

Top3 recommendTop3(Db& db, const Ddragon& dd, const DraftContext& ctx) {
    Top3 out;
    out.patch = ctx.patch.empty() ? dd.version() : ctx.patch;

    Profile prof = db.loadProfile();
    bool learnMode = prof.mode == AppMode::Aprender;

    // Candidate set: the user's pool for the role (RF-CS-003/004).
    std::set<std::string> pickable(ctx.ownedOrPickable.begin(), ctx.ownedOrPickable.end());
    std::vector<std::string> bans = ctx.allBans();
    std::set<std::string> banned(bans.begin(), bans.end());
    for (auto& c : ctx.allyChampions()) banned.insert(c);
    for (auto& c : ctx.enemyChampions()) banned.insert(c);

    std::vector<Candidate> cands;
    for (auto& e : prof.pool) {
        if (!ctx.role.empty() && !e.role.empty() && e.role != ctx.role) continue;
        if (e.tier == PoolTier::DoNotRecommend) continue;
        if (e.tier == PoolTier::Learning && !learnMode) continue;
        if (banned.count(e.champion)) continue;
        if (!pickable.empty() && !pickable.count(e.champion)) continue;
        if (!dd.champion(e.champion)) continue;   // unknown id in this patch (RF-RUN-002 spirit)
        Candidate c;
        c.entry = e;
        c.prof = proficiency(db, e.champion, ctx.role);
        for (auto& r : db.listMatches(15))
            if (r.userChampion == e.champion) ++c.recentGames;
        cands.push_back(std::move(c));
    }

    if (cands.empty()) {
        out.available = false;
        out.unavailableReason =
            "No hay candidatos validos en tu pool para el rol '" + ctx.role +
            "' (tras excluir bans, picks y 'no recomendar'). Amplia tu pool en Perfil.";
        return out;
    }

    Needs needs = computeNeeds(dd, ctx);
    for (auto& c : cands)
        c.fit = fitScore(dd, c.entry.champion, needs, &c.fitReasons);

    int knownPicks = ctx.knownPicks();
    std::string conf = knownPicks >= 6 ? "media" : "baja";
    out.uncertaintyReason = knownPicks < 6
        ? "Draft incompleto: " + std::to_string(knownPicks) + " de 9 picks visibles"
        : "Sin matchup confirmado de linea; el encaje usa rasgos de composicion";

    auto makeCard = [&](const Candidate& c, const std::string& label,
                        std::vector<std::string> reasons, std::string risk) {
        ChampCard card;
        card.champion = c.entry.champion;
        card.label = label;
        while (reasons.size() > 3) reasons.pop_back();
        card.reasons = std::move(reasons);
        card.risk = std::move(risk);
        card.experience = expLabel(c.recentGames + (int)std::round(c.prof * 15));
        card.confidence = conf;
        return card;
    };

    // Best personal: dominance + fit balance (RF-CS-002 option 1).
    auto best = *std::max_element(cands.begin(), cands.end(), [](auto& a, auto& b) {
        return a.prof * 0.6 + a.fit * 0.4 < b.prof * 0.6 + b.fit * 0.4;
    });
    {
        std::vector<std::string> r = {"Mejor balance entre tu dominio y lo que pide la partida"};
        for (auto& fr : best.fitReasons) r.push_back(fr);
        out.cards.push_back(makeCard(best, "Mejor opcion personal", r,
            best.recentGames == 0 ? "Sin partidas recientes con este campeon" :
                                    "Menor presion si el matchup de linea es desfavorable"));
    }

    // Safe: max proficiency, low difficulty (option 2).
    auto safe = *std::max_element(cands.begin(), cands.end(), [&](auto& a, auto& b) {
        int da = dd.champion(a.entry.champion)->difficulty;
        int dbf = dd.champion(b.entry.champion)->difficulty;
        return a.prof - da * 0.02 < b.prof - dbf * 0.02;
    });
    if (safe.entry.champion != best.entry.champion) {
        out.cards.push_back(makeCard(safe, "Opcion segura",
            {"Tu opcion de menor varianza", "Ejecucion conocida bajo presion"},
            "Puede aportar menos a las carencias de la composicion"));
    }

    // Strategic: max fit on the biggest comp gap (option 3).
    auto strat = *std::max_element(cands.begin(), cands.end(),
                                   [](auto& a, auto& b) { return a.fit < b.fit; });
    if (strat.entry.champion != best.entry.champion &&
        (out.cards.size() < 2 || strat.entry.champion != out.cards[1].champion)) {
        std::vector<std::string> r = {"Cubre la mayor carencia actual: " + needs.ranked.front().second};
        for (auto& fr : strat.fitReasons) r.push_back(fr);
        std::string risk = strat.recentGames < 3 ? "Poca practica reciente: mayor riesgo de ejecucion"
                                                 : "Depende de que el draft rival no cambie el plan";
        if (strat.entry.tier == PoolTier::Learning)
            risk = "Campeon en aprendizaje: objetivo formativo, no de LP (Modo Aprender)";
        out.cards.push_back(makeCard(strat, "Opcion estrategica", r, risk));
    }

    return out;
}

} // namespace rl
