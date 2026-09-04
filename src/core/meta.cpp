#include "core/meta.h"
#include "core/ddragon.h"

#include <algorithm>
#include <array>
#include <map>

namespace rl {

namespace {

// Widening order: ranked on this patch first, then ranked on any patch, then
// normals. Alternate modes never enter (PRD 13.3). The first step that reaches
// the minimum wins; if none does, the largest sample is used and flagged.
std::vector<BuildRow> sampleFor(Db& db, const std::string& champion, const std::string& role,
                                const std::string& patch, bool* older, std::string* queueLabel) {
    struct Step {
        QueueFamily family;
        bool        thisPatch;
        const char* label;
    };
    const Step steps[] = {{QueueFamily::Ranked, true, "ranked"},
                          {QueueFamily::Ranked, false, "ranked"},
                          {QueueFamily::Normal, true, "normales"},
                          {QueueFamily::Normal, false, "normales"}};
    std::vector<BuildRow> best;
    const Step* bestStep = nullptr;
    for (auto& st : steps) {
        auto rows = db.builds(champion, role, st.thisPatch ? patch : std::string(), st.family);
        if ((int)rows.size() >= kMetaMinSample) {
            *older = !st.thisPatch && !patch.empty();
            *queueLabel = st.label;
            return rows;
        }
        if (rows.size() > best.size()) { best = std::move(rows); bestStep = &st; }
    }
    *older = bestStep ? (!bestStep->thisPatch && !patch.empty()) : false;
    *queueLabel = bestStep ? bestStep->label : "";
    return best;
}

double medianOf(std::vector<double> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// Rows played into a comp with the same threat profile. The cut is the median
// of the sample itself, not a fixed number: on a real sample fixed thresholds
// put 57% of comps in one bucket and 1% in another, which separates nothing.
// A median always splits the sample in two comparable halves.
// Rows whose context was never computed (all zeros) are left out: unknown is
// not "no threat".
std::vector<BuildRow> narrowToMatchup(const std::vector<BuildRow>& rows, ThreatContext threat) {
    std::vector<BuildRow> out;
    if (!threat.known) return out;
    std::vector<double> bursts, ccs;
    for (auto& r : rows) {
        if (r.enemyBurst == 0 && r.enemyCc == 0) continue;
        bursts.push_back(r.enemyBurst);
        ccs.push_back(r.enemyCc);
    }
    // Splitting a sample that can barely support one page yields two that
    // support none.
    if ((int)bursts.size() < kMetaMinSample * 2) return out;

    double burstCut = medianOf(bursts);
    double ccCut = medianOf(ccs);
    bool wantBurst = threat.burst >= burstCut;
    bool wantCc = threat.cc >= ccCut;
    for (auto& r : rows) {
        if (r.enemyBurst == 0 && r.enemyCc == 0) continue;
        if ((r.enemyBurst >= burstCut) != wantBurst) continue;
        if ((r.enemyCc >= ccCut) != wantCc) continue;
        out.push_back(r);
    }
    return out;
}

std::string confidenceFor(int games) {
    if (games >= 40) return "alta";
    if (games >= 15) return "media";
    return "baja";
}

MetaSample sampleOf(const std::vector<BuildRow>& rows, const std::string& patch, bool older,
                    const std::string& queueLabel, bool matchupFiltered) {
    MetaSample s;
    s.games = (int)rows.size();
    for (auto& r : rows) {
        if (r.win) ++s.wins;
        if (r.isUser) ++s.userRows;
    }
    s.patch = patch;
    s.fromOlderPatch = older;
    s.queueLabel = queueLabel;
    s.matchupFiltered = matchupFiltered;
    s.confidence = confidenceFor(s.games);
    if (older) s.confidence = "baja";       // mixing patches never earns more
    return s;
}

// Most frequent value, ties broken by the first seen.
int mostCommon(const std::map<int, int>& counts) {
    int best = 0, bestN = 0;
    for (auto& [value, n] : counts)
        if (n > bestN) { best = value; bestN = n; }
    return best;
}

} // namespace

std::string MetaSample::note() const {
    std::string n = "Muestra local: " + std::to_string(games) + " builds";
    if (!queueLabel.empty()) n += " de " + queueLabel;
    if (!patch.empty() && !fromOlderPatch) n += " del parche " + patch;
    if (fromOlderPatch) n += " de parches anteriores (no comparables sin normalizar)";
    if (matchupFiltered) n += " contra una composicion parecida";
    if (userRows > 0) n += ", " + std::to_string(userRows) + " tuyas";
    if (games >= 10) {
        int pct = (int)(100.0 * wins / games + 0.5);
        n += ", " + std::to_string(pct) + "% victorias";
    } else {
        n += ", muestra insuficiente para dar victorias";
    }
    return n;
}

int metaSampleSize(Db& db, const std::string& champion, const std::string& role,
                   const std::string& patch) {
    bool older = false;
    std::string label;
    return (int)sampleFor(db, champion, role, patch, &older, &label).size();
}

std::optional<MetaRunes> metaRunes(Db& db, const std::string& champion, const std::string& role,
                                   const std::string& patch, ThreatContext threat) {
    bool older = false;
    std::string queueLabel;
    auto rows = sampleFor(db, champion, role, patch, &older, &queueLabel);
    // Only pages with a full 6-perk selection are comparable.
    rows.erase(std::remove_if(rows.begin(), rows.end(),
                              [](const BuildRow& r) {
                                  return r.perks.size() < 6 || r.primaryStyle == 0;
                              }),
               rows.end());
    if ((int)rows.size() < kMetaMinSample) return std::nullopt;

    // Prefer what was played INTO a comp like this one. That is what makes the
    // keystone answer the matchup instead of answering popularity. It only
    // applies when the narrowed sample can still support a page.
    bool matchupFiltered = false;
    auto narrowed = narrowToMatchup(rows, threat);
    if ((int)narrowed.size() >= kMetaMinSample) {
        rows = std::move(narrowed);
        matchupFiltered = true;
    }

    // Group by (primary style, sub style, keystone): those are the real
    // decisions. Ordering is by frequency, never by win rate on a small sample.
    std::map<std::array<int, 3>, std::vector<const BuildRow*>> groups;
    for (auto& r : rows) groups[{r.primaryStyle, r.subStyle, r.perks[0]}].push_back(&r);
    const std::vector<const BuildRow*>* top = nullptr;
    for (auto& [key, list] : groups)
        if (!top || list.size() > top->size()) top = &list;
    if (!top || (int)top->size() < kMetaMinPage) return std::nullopt;
    if ((double)top->size() / (double)rows.size() < kMetaMinPageShare) return std::nullopt;

    MetaRunes out;
    out.primaryStyle = (*top)[0]->primaryStyle;
    out.subStyle = (*top)[0]->subStyle;
    // Primary rows are independent: each slot belongs to its own row, so the
    // most common perk per slot is always selectable.
    for (int slot = 0; slot < 4; ++slot) {
        std::map<int, int> counts;
        for (auto* r : *top) ++counts[r->perks[slot]];
        out.perks.push_back(mostCommon(counts));
    }
    // The two secondary perks are NOT independent: they must come from
    // different rows. Picking the most common of each slot separately can
    // invent a pair nobody played and that the client cannot select, so the
    // most common PAIR wins instead.
    std::map<std::pair<int, int>, int> pairCounts;
    for (auto* r : *top) ++pairCounts[{r->perks[4], r->perks[5]}];
    std::pair<int, int> bestPair{0, 0};
    int bestPairN = 0;
    for (auto& [pair, n] : pairCounts)
        if (n > bestPairN) { bestPair = pair; bestPairN = n; }
    out.perks.push_back(bestPair.first);
    out.perks.push_back(bestPair.second);
    std::vector<BuildRow> used;
    for (auto* r : *top) used.push_back(*r);
    out.sample = sampleOf(used, patch, older, queueLabel, matchupFiltered);
    return out;
}

std::optional<MetaItems> metaItems(Db& db, const Ddragon& dd, const std::string& champion,
                                   const std::string& role, const std::string& patch) {
    bool older = false;
    std::string queueLabel;
    auto rows = sampleFor(db, champion, role, patch, &older, &queueLabel);
    if ((int)rows.size() < kMetaMinSample) return std::nullopt;

    std::map<int, int> itemCounts, bootCounts;
    for (auto& r : rows) {
        for (int id : r.items) {
            const ItemInfo* it = dd.item(id);
            if (!it || !it->purchasable) continue;      // gone in this patch
            bool boots = std::find(it->tags.begin(), it->tags.end(), "Boots") != it->tags.end();
            if (boots) ++bootCounts[id];
            else if (it->totalGold >= 2000) ++itemCounts[id];   // completed items only
        }
    }
    if (itemCounts.empty()) return std::nullopt;

    std::vector<std::pair<int, int>> ranked(itemCounts.begin(), itemCounts.end());
    std::sort(ranked.begin(), ranked.end(),
              [](auto& a, auto& b) { return a.second > b.second; });

    MetaItems out;
    for (auto& [id, n] : ranked) {
        if ((int)out.core.size() >= 3) break;
        out.core.push_back(id);
    }
    if (int b = mostCommon(bootCounts)) out.boots.push_back(b);
    out.sample = sampleOf(rows, patch, older, queueLabel, false);
    return out;
}

} // namespace rl
