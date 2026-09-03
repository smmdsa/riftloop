// Data Dragon static data: champions, items, runes, summoner spells.
// Downloads to %LOCALAPPDATA%\RiftLoop\cache\ddragon\<version>\ and loads from
// disk. Tests load from a fixture directory without network.
#pragma once
#include "core/models.h"

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace rl {

struct ChampInfo {
    std::string id;                  // "Thresh"
    std::string name;                // localized display name
    int         key = 0;             // numeric champion id
    std::vector<std::string> tags;   // Fighter, Tank, Mage, ...
    int attack = 0, defense = 0, magic = 0, difficulty = 0;
};

struct ItemInfo {
    int         id = 0;
    std::string name;
    int         totalGold = 0;
    std::vector<std::string> tags;
    bool        purchasable = true;
    int         depth = 1;           // 3 ~ finished item
};

struct RuneStyle {
    int         id = 0;              // 8000 Precision, 8100 Domination, ...
    std::string key;
    std::string name;
    // slot -> perk ids in order
    std::vector<std::vector<int>> slots;
};

class Ddragon {
public:
    // Loads from cache. If the cache is empty and network is allowed, downloads
    // the latest version. Returns false when no data is available at all.
    bool load(bool allowDownload = true);
    // Force load from an explicit directory that holds champion.json, item.json,
    // runesReforged.json, summoner.json and a version.txt (tests, offline).
    bool loadFromDir(const std::filesystem::path& dir);

    const std::string& version() const { return version_; }

    const ChampInfo* champion(const std::string& id) const;   // by ddragon id
    const ChampInfo* championByKey(int key) const;
    std::vector<std::string> championIds() const;
    const ItemInfo* item(int id) const;
    const std::vector<RuneStyle>& runeStyles() const { return styles_; }
    bool  runeExists(int perkId) const;
    // summoner spell name -> numeric key ("SummonerFlash" -> 4)
    std::optional<int> summonerKey(const std::string& name) const;

    // Composition traits for one champion (PRD 11.3, derived from tags + info
    // + a small curated table; approximate by design and labeled as such).
    CompTraits traits(const std::string& champId) const;
    CompTraits teamTraits(const std::vector<std::string>& champIds) const;
    bool championHealsHeavily(const std::string& champId) const;

private:
    bool parseFiles(const std::filesystem::path& dir);
    std::string version_;
    std::map<std::string, ChampInfo> champs_;
    std::map<int, std::string> champByKey_;
    std::map<int, ItemInfo> items_;
    std::vector<RuneStyle> styles_;
    std::set<int> perkIds_;
    std::map<std::string, int> summoners_;
};

} // namespace rl
