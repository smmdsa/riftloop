#include "core/planner.h"
#include "core/ingest.h"

#include <algorithm>
#include <map>
#include <set>

namespace rl {

namespace {

// ---- rune knowledge (ids validated against the installed patch, RF-RUN-002)

constexpr int kPrecision = 8000, kDomination = 8100, kSorcery = 8200,
              kResolve = 8400, kInspiration = 8300;

struct StyleTemplate {
    int style;
    int keystone;
    std::vector<int> minors;         // one per remaining primary slot
};

// Curated per class; every id is checked before use and replaced by the first
// perk of the slot when the patch changed it.
StyleTemplate primaryFor(const std::string& tag) {
    if (tag == "Marksman") return {kPrecision, 8008, {9101, 9104, 8017}};
    if (tag == "Fighter")  return {kPrecision, 8010, {9111, 9105, 8299}};
    if (tag == "Assassin") return {kDomination, 8112, {8143, 8138, 8135}};
    if (tag == "Mage")     return {kSorcery, 8229, {8226, 8210, 8237}};
    if (tag == "Tank")     return {kResolve, 8439, {8446, 8429, 8451}};
    if (tag == "Support")  return {kResolve, 8465, {8463, 8473, 8453}};
    return {kSorcery, 8229, {8226, 8210, 8237}};
}

// Fix ids that no longer exist: use the perk actually present in that slot.
void validatePage(const Ddragon& dd, RunePage& page, bool* degraded) {
    const RuneStyle* prim = nullptr;
    const RuneStyle* sub = nullptr;
    for (auto& s : dd.runeStyles()) {
        if (s.id == page.primaryStyle) prim = &s;
        if (s.id == page.subStyle) sub = &s;
    }
    if (!prim || !sub) { *degraded = true; return; }
    // perks layout: [keystone, p1, p2, p3, s1, s2] + 3 shards (not validated:
    // stat shards are not part of runesReforged.json).
    for (int i = 0; i < 4 && i < (int)page.perks.size(); ++i) {
        if (i >= (int)prim->slots.size()) break;
        if (!dd.runeExists(page.perks[i]) ||
            std::find(prim->slots[i].begin(), prim->slots[i].end(), page.perks[i]) ==
                prim->slots[i].end()) {
            if (!prim->slots[i].empty()) page.perks[i] = prim->slots[i][0];
            *degraded = true;
        }
    }
    for (int i = 4; i < 6 && i < (int)page.perks.size(); ++i) {
        int slot = i - 3;            // secondary uses slots 1..3 (no keystone)
        if (slot >= (int)sub->slots.size()) break;
        bool ok = false;
        for (auto& sl : sub->slots)
            if (std::find(sl.begin(), sl.end(), page.perks[i]) != sl.end()) ok = true;
        if (!dd.runeExists(page.perks[i]) || !ok) {
            if ((int)sub->slots.size() > slot && !sub->slots[slot].empty())
                page.perks[i] = sub->slots[slot][0];
            *degraded = true;
        }
    }
}

std::string mainTag(const Ddragon& dd, const std::string& champ) {
    const ChampInfo* c = dd.champion(champ);
    return c && !c->tags.empty() ? c->tags[0] : "Mage";
}

// ---- item knowledge (ids validated against the installed patch) ------------

struct SituationalSet {
    const char* label;
    const char* condition;
    std::vector<int> ap;             // options for AP champions
    std::vector<int> ad;             // options for AD champions
    std::vector<int> tank;
};

const std::vector<SituationalSet> kSituational = {
    {"Contra curacion", "Si hay 2+ fuentes de curacion fuerte enfrente",
     {3165}, {3033, 3123}, {3075}},
    {"Contra burst magico", "Si el burst magico te elimina antes de jugar",
     {3102}, {3156, 3155}, {3065}},
    {"Contra burst fisico", "Si el daño fisico de golpes te domina",
     {3157}, {3026}, {3143}},
    {"Contra CC en cadena", "Si un CC te elimina de cada pelea",
     {3140}, {3140}, {3193}},
    {"Contra escudos", "Si sus escudos absorben tu daño clave",
     {3135}, {3036}, {3036}},
};

const std::vector<int> kBootsMr    = {3111};
const std::vector<int> kBootsArmor = {3047};
const std::vector<int> kBootsDps   = {3006};
const std::vector<int> kBootsHaste = {3158};
const std::vector<int> kBootsMage  = {3020};

std::vector<int> keepExisting(const Ddragon& dd, const std::vector<int>& ids) {
    std::vector<int> out;
    for (int id : ids)
        if (const ItemInfo* it = dd.item(id); it && it->purchasable) out.push_back(id);
    return out;
}

bool isAp(const Ddragon& dd, const std::string& champ) {
    const ChampInfo* c = dd.champion(champ);
    return c && c->magic > c->attack;
}
bool isTanky(const Ddragon& dd, const std::string& champ) {
    const ChampInfo* c = dd.champion(champ);
    if (!c) return false;
    for (auto& t : c->tags) if (t == "Tank") return true;
    return c->defense >= 8;
}

} // namespace

// ------------------------------------------------------------------- runes

RunePlan planRunes(const Ddragon& dd, const PlanInput& in) {
    RunePlan plan;
    CompTraits enemy = dd.teamTraits(in.draft.enemyChampions);
    std::string tag = mainTag(dd, in.champion);

    StyleTemplate prim = primaryFor(tag);
    bool vsBurst = enemy.burst >= 1.8;
    bool vsCc = enemy.cc >= 2.0;

    RunePage page;
    page.name = "RiftLoop: " + in.champion + " " + in.role;
    page.primaryStyle = prim.style;
    // Secondary: Resolve against burst/CC pressure, Precision/Sorcery otherwise.
    int sub = vsBurst || vsCc ? kResolve
            : prim.style == kPrecision ? kSorcery : kPrecision;
    if (sub == prim.style) sub = kInspiration;
    page.subStyle = sub;

    page.perks = {prim.keystone, prim.minors[0], prim.minors[1], prim.minors[2]};
    if (sub == kResolve) {
        page.perks.push_back(8444);   // Second Wind
        page.perks.push_back(vsCc ? 8242 : 8473);   // Unflinching / Bone Plating
        page.reasons.push_back(vsCc
            ? "Rama Valor con tenacidad: la composicion rival tiene CC en cadena"
            : "Rama Valor defensiva: el burst rival castiga los intercambios largos");
    } else if (sub == kPrecision) {
        page.perks.push_back(9111);
        page.perks.push_back(9104);
        page.reasons.push_back("Precision secundaria para sostener DPS y resets de pelea");
    } else if (sub == kSorcery) {
        page.perks.push_back(8226);
        page.perks.push_back(8236);
        page.reasons.push_back("Hechiceria secundaria: mana y escalado para peleas largas");
    } else {
        page.perks.push_back(8304);
        page.perks.push_back(8347);
        page.reasons.push_back("Inspiracion secundaria: tempo de botas y haste");
    }
    // Stat shards: adaptive + adaptive + (tenacity source not shard) hp.
    page.perks.push_back(5008);
    page.perks.push_back(5008);
    page.perks.push_back(5001);

    bool degraded = false;
    validatePage(dd, page, &degraded);
    plan.main = page;
    plan.confidence = degraded ? "baja" : "media";
    if (degraded)
        plan.main.reasons.push_back(
            "Parche con runas cambiadas: la pagina se ajusto a las runas existentes; revisala");

    // Situational alternative only when a real strategic reason exists (RF-RUN-002).
    if (!vsBurst && enemy.sustainedDps >= 1.5) {
        RunePage alt = page;
        alt.name += " (vs DPS sostenido)";
        alt.subStyle = kResolve;
        alt.perks[4] = 8444;
        alt.perks[5] = 8453;         // Revitalize
        alt.reasons = {"Alternativa contra DPS sostenido: sustain en peleas largas"};
        bool d2 = false;
        validatePage(dd, alt, &d2);
        plan.situational = alt;
    }
    if (plan.main.reasons.size() > 3) plan.main.reasons.resize(3);
    return plan;
}

// ------------------------------------------------------------------ spells

SpellPlan planSpells(const Ddragon& dd, const PlanInput& in) {
    SpellPlan plan;
    CompTraits enemy = dd.teamTraits(in.draft.enemyChampions);

    // Decision priority (RF-SUM-001): real role first, hard constraints next.
    if (in.role == "JUNGLE") {
        plan.spells = {"Flash", "Smite"};
        plan.reason = "Jungla: Smite es una restriccion dura del rol.";
        return plan;
    }
    std::string second = "Teleport";
    std::string why = "Teleport para volver a la linea y jugar cross-map.";
    if (in.role == "BOTTOM") {
        second = enemy.cc >= 2.0 ? "Cleanse" : "Heal";
        why = enemy.cc >= 2.0 ? "Cleanse: el CC dirigido rival elimina a tu carry de cada pelea."
                              : "Heal cubre el all-in de dos rivales en la fase de linea.";
    } else if (in.role == "UTILITY") {
        second = enemy.burst >= 1.8 ? "Exhaust" : "Ignite";
        why = enemy.burst >= 1.8 ? "Exhaust responde al all-in y al burst del rival directo."
                                 : "Ignite convierte la presion de linea en kills reales.";
    } else if (in.role == "MIDDLE" && enemy.burst >= 2.0) {
        second = "Barrier";
        why = "Barrier: la amenaza principal es burst que busca eliminarte al inicio de pelea.";
    }
    // Check the spell exists in this patch's static data.
    if (!dd.summonerKey("Summoner" + second).has_value() &&
        !dd.summonerKey("SummonerTeleport").has_value()) {
        // static data missing: keep names, lower the claim
        why += " (sin validar contra datos estaticos)";
    }
    plan.spells = {"Flash", second};
    plan.reason = why;
    return plan;
}

// ------------------------------------------------------------------- items

ItemPlan planItems(Db& db, const Ddragon& dd, const PlanInput& in) {
    ItemPlan plan;
    CompTraits enemy = dd.teamTraits(in.draft.enemyChampions);
    bool ap = isAp(dd, in.champion);
    bool tank = isTanky(dd, in.champion);

    // Starting items by role/class (validated against the patch).
    if (in.role == "JUNGLE")
        plan.starting = keepExisting(dd, {1103, 2003});          // gustwalker fallback handled below
    else if (in.role == "UTILITY")
        plan.starting = keepExisting(dd, {3865, 2003});
    else if (ap)
        plan.starting = keepExisting(dd, {1056, 2003});
    else
        plan.starting = keepExisting(dd, {1055, 2003});
    if (plan.starting.empty())
        plan.starting = keepExisting(dd, {2003});

    // Core: the user's own most-built finished items on this champion.
    std::map<int, int> counts;
    for (auto& r : db.listMatches(40)) {
        if (r.userChampion != in.champion) continue;
        auto m = parseMatch(db.matchJson(r.matchId));
        if (!m) continue;
        for (auto& p : m->participants) {
            if (p.championName != in.champion) continue;
            for (int id : p.finalItems) {
                const ItemInfo* it = dd.item(id);
                if (it && it->depth >= 3 && it->totalGold >= 2200) ++counts[id];
            }
        }
    }
    std::vector<std::pair<int, int>> ranked(counts.begin(), counts.end());
    std::sort(ranked.begin(), ranked.end(),
              [](auto& a, auto& b) { return a.second > b.second; });
    for (auto& [id, n] : ranked) {
        if (plan.core.size() >= 2) break;
        if (n >= 2) plan.core.push_back(id);
    }
    plan.datasetNote = plan.core.empty()
        ? "Sin historial propio suficiente con " + in.champion +
          ": el nucleo queda abierto. Importa mas partidas para un plan personal."
        : "Nucleo derivado de tus " + std::to_string(ranked.empty() ? 0 : ranked[0].second) +
          "+ compras finalizadas con " + in.champion + " en tu historial local (muestra pequeña).";
    plan.confidence = plan.core.empty() ? "baja" : "media";
    plan.firstBackGold = 900;
    plan.firstBack = {};             // components depend on core; leave open when unknown

    // Boots by threat (RF-OVR-001 example).
    double magicShare = enemy.magical / (enemy.magical + enemy.physical + 0.01);
    auto addBoots = [&](const std::vector<int>& ids, const char* label, const char* cond) {
        auto ok = keepExisting(dd, ids);
        if (!ok.empty()) plan.boots.push_back({label, ok, cond});
    };
    if (magicShare > 0.45 || enemy.cc >= 2.0)
        addBoots(kBootsMr, "Mercury's Treads", "Si el CC o el daño magico sigue siendo la amenaza");
    if (magicShare < 0.65)
        addBoots(kBootsArmor, "Plated Steelcaps", "Si domina el daño fisico de autoataques");
    addBoots(ap ? kBootsMage : (tank ? kBootsHaste : kBootsDps), "Bota ofensiva",
             "Si vas delante y no te alcanzan");
    while (plan.boots.size() > 3) plan.boots.pop_back();

    // Situational branches, max 3, each with its trigger (RF-ITEM-001).
    auto pickSet = [&](const SituationalSet& s) {
        const std::vector<int>& ids = tank ? s.tank : (ap ? s.ap : s.ad);
        auto ok = keepExisting(dd, ids);
        if (!ok.empty()) plan.branches.push_back({s.label, ok, s.condition});
    };
    if (enemy.healingShields >= 2) pickSet(kSituational[0]);
    if (magicShare > 0.5 && enemy.burst >= 1.5) pickSet(kSituational[1]);
    if (magicShare < 0.5 && enemy.burst >= 1.5) pickSet(kSituational[2]);
    if (plan.branches.size() < 3 && enemy.cc >= 2.5) pickSet(kSituational[3]);
    if (plan.branches.size() < 3 && enemy.healingShields < 2 && plan.branches.empty())
        pickSet(magicShare > 0.5 ? kSituational[1] : kSituational[2]);
    while (plan.branches.size() > 3) plan.branches.pop_back();

    return plan;
}

// -------------------------------------------------------------------- quiz

std::vector<QuizQuestion> buildQuiz(const Ddragon& dd, const PlanInput& in) {
    std::vector<QuizQuestion> qs;
    if (in.draft.enemyChampions.empty()) return qs;

    CompTraits enemy = dd.teamTraits(in.draft.enemyChampions);
    CompTraits ally = dd.teamTraits(in.draft.allyChampions);

    // Q1: main enemy damage source (RF-QUIZ-002).
    {
        QuizQuestion q;
        q.conceptTag = "amenaza-principal";
        q.text = "¿Cual es la fuente principal de daño del equipo rival?";
        q.options = {"Fisico", "Magico", "Mixto"};
        double total = enemy.physical + enemy.magical + 0.01;
        double phys = enemy.physical / total;
        q.correctIndex = phys > 0.62 ? 0 : phys < 0.38 ? 1 : 2;
        q.explanation = "Suma los perfiles de daño de los cinco picks; define tus resistencias.";
        qs.push_back(q);
    }
    // Q2: who carries the CC threat.
    {
        std::string best;
        double bestCc = 0;
        for (auto& e : in.draft.enemyChampions) {
            double cc = dd.traits(e).cc;
            if (cc > bestCc) { bestCc = cc; best = e; }
        }
        if (!best.empty() && in.draft.enemyChampions.size() >= 2) {
            QuizQuestion q;
            q.conceptTag = "cc-a-respetar";
            q.text = "¿Que campeon rival concentra el CC que debes respetar?";
            for (auto& e : in.draft.enemyChampions) {
                q.options.push_back(e);
                if (e == best) q.correctIndex = (int)q.options.size() - 1;
                if (q.options.size() == 4) break;
            }
            q.explanation = best + " tiene el perfil de control mas alto del draft rival.";
            qs.push_back(q);
        }
    }
    // Q3: team plan (engage / peel / poke).
    {
        QuizQuestion q;
        q.conceptTag = "plan-de-equipo";
        q.text = "¿Que quiere hacer tu composicion en las peleas?";
        q.options = {"Engage: iniciar la pelea", "Peel: proteger a tus carries",
                     "Poke: desgastar antes de pelear"};
        if (ally.engage >= 1.2 && ally.engage >= ally.peel) q.correctIndex = 0;
        else if (ally.peel >= 1.0) q.correctIndex = 1;
        else q.correctIndex = 2;
        q.explanation = "Se deriva de los rasgos de engage/peel de tus cinco picks.";
        qs.push_back(q);
    }
    // Q4 (only if a slot remains free and the threat is real): defensive boots.
    if (qs.size() < 3) {
        QuizQuestion q;
        q.conceptTag = "botas-defensivas";
        q.text = "¿Que condicion activa botas defensivas en esta partida?";
        double magicShare = enemy.magical / (enemy.magical + enemy.physical + 0.01);
        q.options = {"CC/daño magico dominante -> Mercury's",
                     "Daño fisico de autoataques -> Steelcaps",
                     "Ninguna: vas delante y no te alcanzan"};
        q.correctIndex = (magicShare > 0.5 || enemy.cc >= 2.0) ? 0 : 1;
        q.explanation = "Las botas responden a la amenaza que mas te elimina, no a un habito.";
        qs.push_back(q);
    }
    if (qs.size() > 3) qs.resize(3);     // hard cap (PRD 10.2)
    return qs;
}

} // namespace rl
