#include "core/ddragon.h"
#include "core/http.h"
#include "core/util.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace fs = std::filesystem;
using nlohmann::json;

namespace rl {

namespace {

constexpr const char* kHost = "ddragon.leagueoflegends.com";

// Champions whose kit gives heavy healing/shielding; drives the anti-heal
// branch (RF-ITEM). Curated, not exhaustive; label output as approximate.
const std::set<std::string> kHealers = {
    "Aatrox", "Soraka", "Yuumi", "Sylas", "Vladimir", "DrMundo", "Mundo",
    "Swain", "Illaoi", "Fiora", "Maokai", "WarwicK", "Warwick", "Nami",
    "Sona", "Seraphine", "Kayn", "Rhaast", "Briar", "Zac", "Volibear",
    "Olaf", "Yorick", "KSante", "Renekton"
};

bool downloadTo(const std::string& path, const fs::path& dest) {
    auto r = http::get(kHost, 443, true, path);
    if (r.status != 200) return false;
    return util::writeFile(dest, r.body);
}

} // namespace

bool Ddragon::load(bool allowDownload) {
    fs::path root = util::cacheDir() / "ddragon";

    // Try the newest cached version first.
    std::vector<fs::path> cached;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(root, ec))
        if (e.is_directory()) cached.push_back(e.path());
    std::sort(cached.rbegin(), cached.rend());

    if (allowDownload) {
        auto vr = http::get(kHost, 443, true, "/api/versions.json");
        if (vr.status == 200) {
            try {
                std::string latest = json::parse(vr.body).at(0).get<std::string>();
                fs::path dir = root / latest;
                bool complete = fs::exists(dir / "champion.json") &&
                                fs::exists(dir / "item.json") &&
                                fs::exists(dir / "runesReforged.json") &&
                                fs::exists(dir / "summoner.json");
                if (!complete) {
                    std::string base = "/cdn/" + latest + "/data/en_US/";
                    complete = downloadTo(base + "champion.json", dir / "champion.json") &&
                               downloadTo(base + "item.json", dir / "item.json") &&
                               downloadTo(base + "runesReforged.json", dir / "runesReforged.json") &&
                               downloadTo(base + "summoner.json", dir / "summoner.json");
                    util::writeFile(dir / "version.txt", latest);
                }
                if (complete && parseFiles(dir)) { version_ = latest; return true; }
            } catch (...) {}
        }
    }
    for (auto& dir : cached) {
        if (parseFiles(dir)) {
            version_ = dir.filename().string();
            return true;
        }
    }
    return false;
}

bool Ddragon::loadFromDir(const fs::path& dir) {
    if (!parseFiles(dir)) return false;
    std::string v = util::readFile(dir / "version.txt");
    while (!v.empty() && (v.back() == '\n' || v.back() == '\r')) v.pop_back();
    version_ = v.empty() ? dir.filename().string() : v;
    return true;
}

bool Ddragon::parseFiles(const fs::path& dir) {
    try {
        json champ = json::parse(util::readFile(dir / "champion.json"));
        json item  = json::parse(util::readFile(dir / "item.json"));
        json runes = json::parse(util::readFile(dir / "runesReforged.json"));
        json summ  = json::parse(util::readFile(dir / "summoner.json"));

        champs_.clear(); champByKey_.clear(); items_.clear();
        styles_.clear(); perkIds_.clear(); summoners_.clear();

        for (auto& [id, c] : champ.at("data").items()) {
            ChampInfo ci;
            ci.id   = id;
            ci.name = c.value("name", id);
            ci.key  = std::stoi(c.value("key", "0"));
            for (auto& t : c.value("tags", json::array())) ci.tags.push_back(t);
            if (c.contains("info")) {
                ci.attack     = c["info"].value("attack", 0);
                ci.defense    = c["info"].value("defense", 0);
                ci.magic      = c["info"].value("magic", 0);
                ci.difficulty = c["info"].value("difficulty", 0);
            }
            champByKey_[ci.key] = id;
            champs_[id] = std::move(ci);
        }

        for (auto& [idStr, it] : item.at("data").items()) {
            ItemInfo ii;
            ii.id        = std::stoi(idStr);
            ii.name      = it.value("name", "");
            ii.totalGold = it.contains("gold") ? it["gold"].value("total", 0) : 0;
            ii.purchasable = it.contains("gold") ? it["gold"].value("purchasable", true) : true;
            for (auto& t : it.value("tags", json::array())) ii.tags.push_back(t);
            ii.depth = it.value("depth", 1);
            items_[ii.id] = std::move(ii);
        }

        for (auto& st : runes) {
            RuneStyle rs;
            rs.id   = st.value("id", 0);
            rs.key  = st.value("key", "");
            rs.name = st.value("name", "");
            for (auto& slot : st.value("slots", json::array())) {
                std::vector<int> perks;
                for (auto& r : slot.value("runes", json::array())) {
                    int pid = r.value("id", 0);
                    perks.push_back(pid);
                    perkIds_.insert(pid);
                }
                rs.slots.push_back(std::move(perks));
            }
            styles_.push_back(std::move(rs));
        }

        for (auto& [id, sp] : summ.at("data").items())
            summoners_[id] = std::stoi(sp.value("key", "0"));

        return !champs_.empty() && !items_.empty() && !styles_.empty();
    } catch (...) {
        return false;
    }
}

const ChampInfo* Ddragon::champion(const std::string& id) const {
    auto it = champs_.find(id);
    return it == champs_.end() ? nullptr : &it->second;
}

const ChampInfo* Ddragon::championByKey(int key) const {
    auto it = champByKey_.find(key);
    return it == champByKey_.end() ? nullptr : champion(it->second);
}

std::vector<std::string> Ddragon::championIds() const {
    std::vector<std::string> out;
    out.reserve(champs_.size());
    for (auto& [id, _] : champs_) out.push_back(id);
    return out;
}

const ItemInfo* Ddragon::item(int id) const {
    auto it = items_.find(id);
    return it == items_.end() ? nullptr : &it->second;
}

bool Ddragon::runeExists(int perkId) const { return perkIds_.count(perkId) > 0; }

std::optional<int> Ddragon::summonerKey(const std::string& name) const {
    auto it = summoners_.find(name);
    if (it == summoners_.end()) return std::nullopt;
    return it->second;
}

CompTraits Ddragon::traits(const std::string& champId) const {
    CompTraits t;
    const ChampInfo* c = champion(champId);
    if (!c) return t;

    t.physical = c->attack / 10.0;
    t.magical  = c->magic / 10.0;

    for (auto& tag : c->tags) {
        if (tag == "Tank")     { t.frontline += 1.0; t.cc += 0.7; t.engage += 0.6; }
        if (tag == "Fighter")  { t.frontline += 0.6; t.sustainedDps += 0.5; }
        if (tag == "Mage")     { t.cc += 0.5; t.burst += 0.6; t.waveclear += 0.6; t.range += 0.5; }
        if (tag == "Assassin") { t.burst += 1.0; t.earlyPower += 0.3; }
        if (tag == "Marksman") { t.sustainedDps += 1.0; t.range += 0.8; t.scaling += 0.7; }
        if (tag == "Support")  { t.peel += 0.8; t.cc += 0.4; t.disengage += 0.4; }
    }
    if (c->defense >= 7) t.frontline += 0.4;
    if (c->difficulty >= 8) t.scaling += 0.2;
    if (championHealsHeavily(champId)) t.healingShields += 1.0;
    return t;
}

CompTraits Ddragon::teamTraits(const std::vector<std::string>& champIds) const {
    CompTraits sum;
    for (auto& id : champIds) {
        CompTraits t = traits(id);
        sum.physical += t.physical; sum.magical += t.magical;
        sum.burst += t.burst; sum.sustainedDps += t.sustainedDps;
        sum.frontline += t.frontline; sum.engage += t.engage;
        sum.disengage += t.disengage; sum.peel += t.peel;
        sum.cc += t.cc; sum.range += t.range; sum.waveclear += t.waveclear;
        sum.earlyPower += t.earlyPower; sum.scaling += t.scaling;
        sum.healingShields += t.healingShields;
    }
    return sum;
}

bool Ddragon::championHealsHeavily(const std::string& champId) const {
    return kHealers.count(champId) > 0;
}

} // namespace rl
