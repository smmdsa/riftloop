#include "core/planner.h"
#include "core/ingest.h"
#include "core/meta.h"
#include "core/util.h"

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

// A page is legal when every perk can actually be selected in the client:
// - the keystone comes from row 0 of the primary style,
// - each primary minor comes from its own row (1, 2, 3),
// - the two secondary perks come from DIFFERENT rows of the sub style,
// - each stat shard comes from its own row.
//
// The old version only checked that a perk existed somewhere in the tree, so it
// accepted two secondaries of the same row - a page the client cannot select.
void validateRunePageImpl(const Ddragon& dd, RunePage& page, bool* degraded) {
    const RuneStyle* prim = nullptr;
    const RuneStyle* sub = nullptr;
    for (auto& st : dd.runeStyles()) {
        if (st.id == page.primaryStyle) prim = &st;
        if (st.id == page.subStyle) sub = &st;
    }
    if (!prim || !sub || page.perks.size() < 9) { *degraded = true; return; }

    // Primary: keystone plus one perk per row.
    for (int i = 0; i < 4; ++i) {
        if (i >= (int)prim->slots.size()) break;
        if (dd.perkSlot(prim->id, page.perks[i]) != i) {
            if (!prim->slots[i].empty()) page.perks[i] = prim->slots[i][0];
            *degraded = true;
        }
    }

    // Secondary: two perks, rows 1..3, never the same row twice.
    int rowA = dd.perkSlot(sub->id, page.perks[4]);
    int rowB = dd.perkSlot(sub->id, page.perks[5]);
    if (rowA < 1) {
        page.perks[4] = 0;
        rowA = -1;
        *degraded = true;
    }
    if (rowB < 1 || rowB == rowA) {
        page.perks[5] = 0;
        rowB = -1;
        *degraded = true;
    }
    // Fill whatever is missing from a row that is still free.
    for (int idx = 4; idx <= 5; ++idx) {
        if (page.perks[idx] != 0) continue;
        int taken = dd.perkSlot(sub->id, page.perks[idx == 4 ? 5 : 4]);
        for (size_t row = 1; row < sub->slots.size(); ++row) {
            if ((int)row == taken || sub->slots[row].empty()) continue;
            page.perks[idx] = sub->slots[row][0];
            break;
        }
    }

    // Shards: one per row.
    const auto& shardRows = dd.shardRows();
    for (size_t i = 0; i < shardRows.size() && 6 + i < page.perks.size(); ++i) {
        const auto& row = shardRows[i];
        if (std::find(row.begin(), row.end(), page.perks[6 + i]) == row.end()) {
            page.perks[6 + i] = row.front();
            *degraded = true;
        }
    }
}

// Picks two secondary perks from different rows, honouring the order of
// `wanted`. Anything that would repeat a row is skipped, and free rows fill the
// rest. Returns the pair actually chosen.
std::pair<int, int> pickSecondary(const Ddragon& dd, int subStyle,
                                  const std::vector<int>& wanted) {
    int chosen[2] = {0, 0};
    int rows[2] = {-1, -1};
    int n = 0;
    for (int perk : wanted) {
        if (n == 2) break;
        int row = dd.perkSlot(subStyle, perk);
        if (row < 1) continue;                       // row 0 is the keystone row
        if (n == 1 && row == rows[0]) continue;      // same row: cannot pick both
        chosen[n] = perk;
        rows[n] = row;
        ++n;
    }
    if (n < 2) {
        const RuneStyle* sub = nullptr;
        for (auto& st : dd.runeStyles())
            if (st.id == subStyle) sub = &st;
        if (sub) {
            for (size_t row = 1; row < sub->slots.size() && n < 2; ++row) {
                if ((int)row == rows[0] || sub->slots[row].empty()) continue;
                chosen[n] = sub->slots[row][0];
                rows[n] = (int)row;
                ++n;
            }
        }
    }
    return {chosen[0], chosen[1]};
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
// The pattern a primary tree goes for. One line each, and they must not read
// alike: the user compares two pages by these words (RF-RUN-002).
const char* primaryPattern(int style) {
    switch (style) {
        case kPrecision:  return "dano sostenido y peleas largas";
        case kDomination: return "picos de dano contra el objetivo debil";
        case kSorcery:    return "escalado y alcance de habilidades";
        case kResolve:    return "aguante en linea y desgaste";
        default:          return "tempo temprano y economia";   // kInspiration
    }
}

bool isTanky(const Ddragon& dd, const std::string& champ) {
    const ChampInfo* c = dd.champion(champ);
    if (!c) return false;
    for (auto& t : c->tags) if (t == "Tank") return true;
    return c->defense >= 8;
}

} // namespace

// ------------------------------------------------------------------- runes

void validateRunePage(const Ddragon& dd, RunePage& page, bool* degraded) {
    validateRunePageImpl(dd, page, degraded);
}

RunePlan planRunes(Db& db, const Ddragon& dd, const PlanInput& in) {
    RunePlan plan;
    CompTraits enemy = dd.teamTraits(in.draft.enemyChampions());
    std::string tag = mainTag(dd, in.champion);

    StyleTemplate prim = primaryFor(tag);
    bool vsBurst = enemy.burst >= 1.8;
    bool vsCc = enemy.cc >= 2.0;

    // Data first: the page most played on this champion and role in the local
    // sample. It replaces the class template, not the matchup adjustment below.
    std::string family = util::patchFamily(dd.version());
    // The sample is asked for what was played into a comp like this one, so
    // the keystone answers the matchup and not only popularity.
    ThreatContext threat;
    threat.known = !in.draft.enemyChampions().empty();
    threat.burst = enemy.burst;
    threat.cc = enemy.cc;
    auto meta = metaRunes(db, in.champion, in.role, family, threat);
    std::string metaNote;
    if (meta) {
        prim.style = meta->primaryStyle;
        prim.keystone = meta->perks[0];
        prim.minors = {meta->perks[1], meta->perks[2], meta->perks[3]};
        metaNote = "Base: pagina mas jugada en tus partidas. " + meta->sample.note();
    }

    RunePage page;
    page.name = "RiftLoop: " + in.champion + " " + in.role;
    page.primaryStyle = prim.style;
    // Secondary: Resolve against burst/CC pressure, Precision/Sorcery otherwise.
    int sub = vsBurst || vsCc ? kResolve
            : meta ? meta->subStyle
            : prim.style == kPrecision ? kSorcery : kPrecision;
    if (sub == prim.style) sub = kInspiration;
    page.subStyle = sub;

    // Did the matchup pick the secondary tree, or did the sample? Saying "the
    // enemy burst punishes you" with no enemy picked would be a claim with no
    // evidence behind it (PRD 3.3).
    bool subFromThreat = vsBurst || vsCc;
    page.perks = {prim.keystone, prim.minors[0], prim.minors[1], prim.minors[2]};
    // Candidates in order of preference. pickSecondary drops anything that
    // would repeat a row, so a pair the client cannot select never leaves here.
    std::vector<int> wanted;
    if (sub == kResolve) {
        // 8444 Second Wind and 8473 Bone Plating share row 2: asking for both
        // produced a page nobody could select. 8242 Unflinching is row 3.
        wanted = vsCc ? std::vector<int>{8444, 8242} : std::vector<int>{8473, 8242, 8444};
        page.reasons.push_back(
            !subFromThreat ? "Rama Valor de la pagina mas jugada, no de la amenaza rival"
            : vsCc         ? "Rama Valor con tenacidad: la composicion rival tiene CC en cadena"
                           : "Rama Valor defensiva: el burst rival castiga los intercambios largos");
    } else if (sub == kPrecision) {
        wanted = {9111, 9104};
        page.reasons.push_back("Precision secundaria para sostener DPS y resets de pelea");
    } else if (sub == kSorcery) {
        wanted = {8226, 8236};
        page.reasons.push_back("Hechiceria secundaria: mana y escalado para peleas largas");
    } else {
        wanted = {8304, 8347};
        page.reasons.push_back("Inspiracion secundaria: tempo de botas y haste");
    }
    auto [sec1, sec2] = pickSecondary(dd, sub, wanted);
    page.perks.push_back(sec1);
    page.perks.push_back(sec2);
    if (meta && page.subStyle == meta->subStyle && !subFromThreat) {
        // The sample answers the secondary slots only when no enemy threat
        // dictated them. A matchup adjustment outranks frequency, and the
        // stated reason must match the perks that end up on the page.
        auto [m1, m2] = pickSecondary(dd, page.subStyle, {meta->perks[4], meta->perks[5]});
        page.perks[4] = m1;
        page.perks[5] = m2;
    }
    if (!metaNote.empty()) page.reasons.insert(page.reasons.begin(), metaNote);

    // Stat shards: adaptive + adaptive + hp. The third slot is the only real
    // decision here, so it is the one that gets explained.
    page.perks.push_back(5008);
    page.perks.push_back(vsBurst || vsCc ? 5001 : 5008);
    page.perks.push_back(5001);
    page.reasons.push_back(vsBurst || vsCc
        ? "Fragmento de vida en el segundo slot: la amenaza rival es burst o CC, no DPS"
        : "Dos fragmentos adaptativos: nada en la comp rival obliga a cambiar por vida");

    // What this page goes for. The primary tree names the pattern; the second
    // half says what the page pays for it, and only when a real enemy trait
    // forced the payment (PRD 3.3).
    page.intent = std::string("Busca ") + primaryPattern(page.primaryStyle) + ", y " +
                  (subFromThreat ? (vsCc ? "paga tenacidad contra el CC rival"
                                         : "paga aguante contra el burst rival")
                                 : "no cede dano: nada en la comp rival lo obliga");

    bool degraded = false;
    validateRunePageImpl(dd, page, &degraded);
    plan.main = page;
    // Confidence follows the sample, never the template.
    plan.confidence = degraded ? "baja" : meta ? meta->sample.confidence : "baja";
    // The three reasons are capped at the end, so the warnings go in front: a
    // reader must not lose them to a template line about shards.
    if (!meta)
        plan.main.reasons.insert(
            plan.main.reasons.begin(),
            "Sin muestra local suficiente: plantilla por clase, no datos de tu parche");
    if (degraded)
        plan.main.reasons.insert(
            plan.main.reasons.begin(),
            "Parche con runas cambiadas: la pagina se ajusto a las runas existentes; revisala");

    plan.draftClosed = in.draft.closed();
    plan.missingPicks = in.draft.missingPicks();
    // A page built on an open draft can still be invalidated by the next pick.
    if (!plan.draftClosed && plan.confidence == "alta") plan.confidence = "media";

    // Situational alternative only when a real strategic reason exists
    // (RF-RUN-002: an alternative must change for a strategy, not for variety).
    // Each candidate names the threat it answers and the condition that fires it.
    struct AltCandidate {
        bool        applies;
        const char* suffix;
        int         perk4;
        int         perk5;
        const char* reason;
        // The trade this page makes. It must name what the user gives up, or
        // the two pages read as the same choice with different icons.
        const char* intent;
    };
    const AltCandidate candidates[] = {
        // Each pair must come from different rows of Resolve: 8444 is row 2,
        // 8242/8451/8453 are row 3, 8473 is row 2.
        {enemy.cc >= 2.0, " (vs CC en cadena)", 8444, 8242,
         "Alternativa con tenacidad: si el CC dirigido decide las peleas, no el daño",
         "Cambia dano por tenacidad: sales del CC antes y llegas a usar tu kit"},
        {enemy.burst >= 1.8, " (vs burst)", 8473, 8451,
         "Alternativa defensiva: si el burst rival te elimina antes de que juegues",
         "Cambia dano por aguante: sobrevives al primer golpe y respondes despues"},
        {enemy.sustainedDps >= 1.5, " (vs DPS sostenido)", 8444, 8453,
         "Alternativa con sustain: si las peleas largas se deciden por regeneracion",
         "Cambia dano por regeneracion: ganas los intercambios que se alargan"},
    };
    for (auto& c : candidates) {
        if (!c.applies) continue;
        auto [a1, a2] = pickSecondary(dd, kResolve, {c.perk4, c.perk5});
        // An alternative equal to the main page teaches nothing.
        if (page.subStyle == kResolve && page.perks[4] == a1 && page.perks[5] == a2) continue;
        RunePage alt = page;
        alt.name += c.suffix;
        alt.subStyle = kResolve;
        alt.perks[4] = a1;
        alt.perks[5] = a2;
        alt.reasons = {c.reason};
        alt.intent = c.intent;
        bool d2 = false;
        validateRunePageImpl(dd, alt, &d2);
        plan.situational = alt;
        break;
    }
    if (!plan.situational)
        plan.noAlternativeReason =
            plan.draftClosed
                ? "La composicion rival no presenta una amenaza que justifique otra pagina"
                : "Faltan " + std::to_string(plan.missingPicks) +
                      " picks: la alternativa se decide con el draft cerrado";

    if (plan.main.reasons.size() > 3) plan.main.reasons.resize(3);
    return plan;
}

// ------------------------------------------------------------------ spells

SpellPlan planSpells(const Ddragon& dd, const PlanInput& in) {
    SpellPlan plan;
    CompTraits enemy = dd.teamTraits(in.draft.enemyChampions());

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
    CompTraits enemy = dd.teamTraits(in.draft.enemyChampions());
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

    // Core: the local meta sample for this champion and role. It reads the
    // builds table (every participant of every imported match), not only the
    // user's own games, and it never reparses the stored match json here.
    auto meta = metaItems(db, dd, in.champion, in.role, util::patchFamily(dd.version()));
    if (meta) {
        for (int id : meta->core) {
            if (plan.core.size() >= 2) break;
            plan.core.push_back(id);
        }
        plan.datasetNote = "Nucleo por frecuencia, no por win rate. " + meta->sample.note() +
                           ". Sesgo conocido: tus partidas, tu elo y tu region.";
        plan.confidence = meta->sample.confidence;
    } else {
        plan.datasetNote = "Sin muestra local suficiente con " + in.champion + " en " +
                           (in.role.empty() ? "ese rol" : in.role) +
                           ": el nucleo queda abierto. Importa mas partidas.";
        plan.confidence = "baja";
    }
    plan.firstBackGold = 900;
    plan.firstBack = {};             // components depend on core; leave open when unknown

    // Boots by threat (RF-OVR-001 example).
    double magicShare = enemy.magical / (enemy.magical + enemy.physical + 0.01);
    auto addBoots = [&](const std::vector<int>& ids, const char* label, const char* cond) {
        auto ok = keepExisting(dd, ids);
        if (!ok.empty()) plan.boots.push_back({label, ok, cond});
    };
    if (meta && !meta->boots.empty())
        addBoots(meta->boots, "Botas mas frecuentes", "Si la partida no cambia la amenaza");
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
    if (in.draft.enemyChampions().empty()) return qs;

    CompTraits enemy = dd.teamTraits(in.draft.enemyChampions());
    CompTraits ally = dd.teamTraits(in.draft.allyChampions());

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
        for (auto& e : in.draft.enemyChampions()) {
            double cc = dd.traits(e).cc;
            if (cc > bestCc) { bestCc = cc; best = e; }
        }
        if (!best.empty() && in.draft.enemyChampions().size() >= 2) {
            QuizQuestion q;
            q.conceptTag = "cc-a-respetar";
            q.text = "¿Que campeon rival concentra el CC que debes respetar?";
            auto display = [&](const std::string& id) {
                const ChampInfo* c = dd.champion(id);
                return c ? c->name : id;     // localized name
            };
            for (auto& e : in.draft.enemyChampions()) {
                q.options.push_back(display(e));
                if (e == best) q.correctIndex = (int)q.options.size() - 1;
                if (q.options.size() == 4) break;
            }
            q.explanation = display(best) + " tiene el perfil de control mas alto del draft rival.";
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
