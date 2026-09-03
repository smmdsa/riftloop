#include "core/patchimpact.h"
#include "core/util.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;
using nlohmann::json;

namespace rl {

namespace {

// Base-stat snapshot for one champion from a cached champion.json.
json statsFor(const fs::path& dir, const std::string& champ) {
    try {
        json j = json::parse(util::readFile(dir / "champion.json"));
        if (j.contains("data") && j["data"].contains(champ))
            return j["data"][champ].value("stats", json::object());
    } catch (...) {}
    return json::object();
}

} // namespace

PatchImpactReport patchImpact(Db& db, const Ddragon& dd) {
    PatchImpactReport rep;
    rep.toVersion = dd.version();
    rep.fromVersion = db.getKv("previous_data_version");

    std::string lastSeen = db.getKv("last_data_version");
    if (lastSeen != dd.version()) {
        if (!lastSeen.empty()) db.setKv("previous_data_version", lastSeen);
        db.setKv("last_data_version", dd.version());
        rep.fromVersion = lastSeen;
    }

    Profile prof = db.loadProfile();
    if (prof.pool.empty()) {
        rep.note = "Configura tu champion pool en Perfil para ver el impacto del parche.";
        return rep;
    }

    fs::path root = util::cacheDir() / "ddragon";
    fs::path oldDir = rep.fromVersion.empty() ? fs::path{} : root / rep.fromVersion;
    fs::path newDir = root / dd.version();
    bool canDiff = !rep.fromVersion.empty() && fs::exists(oldDir / "champion.json") &&
                   fs::exists(newDir / "champion.json");

    std::vector<std::string> seen;
    for (auto& e : prof.pool) {
        if (std::find(seen.begin(), seen.end(), e.champion) != seen.end()) continue;
        seen.push_back(e.champion);
        PatchImpactEntry entry;
        entry.champion = e.champion;
        if (canDiff) {
            json oldStats = statsFor(oldDir, e.champion);
            json newStats = statsFor(newDir, e.champion);
            std::string diffs;
            for (auto& [k, v] : newStats.items()) {
                if (oldStats.contains(k) && oldStats[k] != v) {
                    if (!diffs.empty()) diffs += ", ";
                    diffs += k + ": " + oldStats[k].dump() + " -> " + v.dump();
                }
            }
            if (!diffs.empty()) {
                entry.direct = true;
                entry.change = "cambio directo en stats base (" + diffs + ")";
            } else {
                entry.change = "sin cambio directo en stats base; puede haber cambios de "
                               "habilidades, objetos o matchups no visibles aqui";
            }
        } else {
            entry.change = "sin snapshot anterior para comparar; se marca como datos tempranos";
        }
        rep.entries.push_back(std::move(entry));
    }

    rep.note = "Datos tempranos: no abandones un main por el win rate de las primeras horas "
               "(PRD 9.17). Prueba primero en normales los campeones con cambio directo.";
    return rep;
}

} // namespace rl
