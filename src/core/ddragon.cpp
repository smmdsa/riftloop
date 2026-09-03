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

bool filesComplete(const fs::path& dir) {
    return fs::exists(dir / "champion.json") && fs::exists(dir / "item.json") &&
           fs::exists(dir / "runesReforged.json") && fs::exists(dir / "summoner.json");
}

bool downloadSet(const std::string& version, const std::string& locale, const fs::path& dir) {
    if (filesComplete(dir)) return true;
    std::string base = "/cdn/" + version + "/data/" + locale + "/";
    bool ok = downloadTo(base + "champion.json", dir / "champion.json") &&
              downloadTo(base + "item.json", dir / "item.json") &&
              downloadTo(base + "runesReforged.json", dir / "runesReforged.json") &&
              downloadTo(base + "summoner.json", dir / "summoner.json");
    if (ok) util::writeFile(dir / "version.txt", version);
    return ok;
}

} // namespace

bool Ddragon::load(bool allowDownload, const std::string& locale) {
    fs::path root = util::cacheDir() / "ddragon";
    std::string wantLocale = locale.empty() ? "en_US" : locale;

    if (allowDownload) {
        auto vr = http::get(kHost, 443, true, "/api/versions.json");
        if (vr.status == 200) {
            try {
                std::string latest = json::parse(vr.body).at(0).get<std::string>();
                fs::path dir = root / latest / wantLocale;
                if (downloadSet(latest, wantLocale, dir) && parseFiles(dir)) {
                    version_ = latest;
                    locale_ = wantLocale;
                    return true;
                }
                // Locale not served: fall back to en_US (RF: degrade, explain).
                if (wantLocale != "en_US") {
                    fs::path en = root / latest / "en_US";
                    if (downloadSet(latest, "en_US", en) && parseFiles(en)) {
                        version_ = latest;
                        locale_ = "en_US";
                        return true;
                    }
                }
            } catch (...) {}
        }
    }

    // Offline: newest cached version, preferred locale first.
    std::vector<fs::path> versions;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(root, ec))
        if (e.is_directory()) versions.push_back(e.path());
    std::sort(versions.rbegin(), versions.rend());
    for (auto& vdir : versions) {
        std::vector<fs::path> candidates = {vdir / wantLocale};
        for (auto& sub : fs::directory_iterator(vdir, ec))
            if (sub.is_directory()) candidates.push_back(sub.path());
        candidates.push_back(vdir);      // legacy flat layout
        for (auto& dir : candidates) {
            if (!filesComplete(dir)) continue;
            if (parseFiles(dir)) {
                version_ = vdir.filename().string();
                locale_ = dir == vdir ? "en_US" : dir.filename().string();
                return true;
            }
        }
    }
    return false;
}

bool Ddragon::loadFromDir(const fs::path& dir) {
    if (!parseFiles(dir)) return false;
    std::string v = util::readFile(dir / "version.txt");
    while (!v.empty() && (v.back() == '\n' || v.back() == '\r')) v.pop_back();
    version_ = v.empty() ? dir.filename().string() : v;
    locale_ = "en_US";
    return true;
}

bool Ddragon::parseFiles(const fs::path& dir) {
    try {
        json champ = json::parse(util::readFile(dir / "champion.json"));
        json item  = json::parse(util::readFile(dir / "item.json"));
        json runes = json::parse(util::readFile(dir / "runesReforged.json"));
        json summ  = json::parse(util::readFile(dir / "summoner.json"));

        champs_.clear(); champByKey_.clear(); items_.clear();
        styles_.clear(); perkIds_.clear(); perkNames_.clear(); perkIcons_.clear();
        summoners_.clear(); summonerNames_.clear();

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
            rs.icon = st.value("icon", "");
            perkNames_[rs.id] = rs.name;
            perkIcons_[rs.id] = rs.icon;
            for (auto& slot : st.value("slots", json::array())) {
                std::vector<int> perks;
                for (auto& r : slot.value("runes", json::array())) {
                    int pid = r.value("id", 0);
                    perks.push_back(pid);
                    perkIds_.insert(pid);
                    perkNames_[pid] = r.value("name", "");
                    perkIcons_[pid] = r.value("icon", "");
                }
                rs.slots.push_back(std::move(perks));
            }
            styles_.push_back(std::move(rs));
        }

        for (auto& [id, sp] : summ.at("data").items()) {
            int key = std::stoi(sp.value("key", "0"));
            summoners_[id] = key;
            summonerNames_[key] = sp.value("name", id);
        }

        return !champs_.empty() && !items_.empty() && !styles_.empty();
    } catch (...) {
        return false;
    }
}

std::string Ddragon::displayPatch() const {
    size_t d1 = version_.find('.');
    if (d1 == std::string::npos) return version_;
    size_t d2 = version_.find('.', d1 + 1);
    std::string minor = version_.substr(d1 + 1, d2 == std::string::npos ? std::string::npos
                                                                        : d2 - d1 - 1);
    try {
        int major = std::stoi(version_.substr(0, d1));
        if (major >= 15) return std::to_string(major + 10) + "." + minor;
    } catch (...) {}
    return version_;
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

std::string Ddragon::perkName(int perkId) const {
    auto it = perkNames_.find(perkId);
    return it != perkNames_.end() && !it->second.empty() ? it->second
                                                         : "#" + std::to_string(perkId);
}

std::string Ddragon::styleName(int styleId) const { return perkName(styleId); }

std::string Ddragon::shardName(int shardId) const {
    // Stat shards are not part of runesReforged.json; curated bilingual names.
    bool es = locale_.rfind("es", 0) == 0;
    switch (shardId) {
        case 5008: return es ? "Fuerza adaptativa" : "Adaptive Force";
        case 5005: return es ? "Velocidad de ataque" : "Attack Speed";
        case 5007: return es ? "Celeridad de habilidades" : "Ability Haste";
        case 5001: return es ? "Vida" : "Health";
        case 5010: return es ? "Velocidad de movimiento" : "Move Speed";
        case 5011: return es ? "Vida" : "Health";
        case 5013: return es ? "Tenacidad" : "Tenacity";
        case 5002: return es ? "Armadura" : "Armor";
        case 5003: return es ? "Resistencia magica" : "Magic Resist";
    }
    return "#" + std::to_string(shardId);
}

std::string Ddragon::summonerNameByKey(int key) const {
    auto it = summonerNames_.find(key);
    return it != summonerNames_.end() ? it->second : "#" + std::to_string(key);
}

std::string Ddragon::summonerNameById(const std::string& id) const {
    auto it = summoners_.find(id);
    return it != summoners_.end() ? summonerNameByKey(it->second) : id;
}

std::string Ddragon::summonerDisplay(const std::string& simpleName) const {
    static const std::map<std::string, std::string> kMap = {
        {"Flash", "SummonerFlash"},   {"Teleport", "SummonerTeleport"},
        {"Heal", "SummonerHeal"},     {"Ignite", "SummonerDot"},
        {"Exhaust", "SummonerExhaust"}, {"Barrier", "SummonerBarrier"},
        {"Cleanse", "SummonerBoost"}, {"Ghost", "SummonerHaste"},
        {"Smite", "SummonerSmite"},
    };
    auto it = kMap.find(simpleName);
    if (it == kMap.end()) return simpleName;
    std::string name = summonerNameById(it->second);
    return name == it->second ? simpleName : name;
}

std::optional<int> Ddragon::summonerKey(const std::string& name) const {
    auto it = summoners_.find(name);
    if (it == summoners_.end()) return std::nullopt;
    return it->second;
}

std::string Ddragon::championIconUrl(const std::string& champId) const {
    return "https://ddragon.leagueoflegends.com/cdn/" + version_ + "/img/champion/" + champId +
           ".png";
}

std::string Ddragon::itemIconUrl(int itemId) const {
    return "https://ddragon.leagueoflegends.com/cdn/" + version_ + "/img/item/" +
           std::to_string(itemId) + ".png";
}

std::string Ddragon::perkIconUrl(int perkOrStyleId) const {
    auto it = perkIcons_.find(perkOrStyleId);
    if (it == perkIcons_.end() || it->second.empty()) return {};
    return "https://ddragon.leagueoflegends.com/cdn/img/" + it->second;
}

std::string Ddragon::spellIconUrl(const std::string& simpleName) const {
    static const std::map<std::string, std::string> kMap = {
        {"Flash", "SummonerFlash"},   {"Teleport", "SummonerTeleport"},
        {"Heal", "SummonerHeal"},     {"Ignite", "SummonerDot"},
        {"Exhaust", "SummonerExhaust"}, {"Barrier", "SummonerBarrier"},
        {"Cleanse", "SummonerBoost"}, {"Ghost", "SummonerHaste"},
        {"Smite", "SummonerSmite"},
    };
    auto it = kMap.find(simpleName);
    if (it == kMap.end()) return {};
    return "https://ddragon.leagueoflegends.com/cdn/" + version_ + "/img/spell/" + it->second +
           ".png";
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
