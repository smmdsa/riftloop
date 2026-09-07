#include "core/rank.h"

#include "core/util.h"

#include <nlohmann/json.hpp>

#include <ctime>

using nlohmann::json;

namespace rl {

namespace {

constexpr const char* kKey = "rank.solo";

std::string toJson(const LcuRank& r) {
    json j;
    j["tier"] = r.tier;
    j["division"] = r.division;
    j["leaguePoints"] = r.leaguePoints;
    j["wins"] = r.wins;
    j["losses"] = r.losses;
    j["queue"] = r.queue;
    j["readAtIso"] = r.readAtIso;
    j["readAtEpoch"] = (int64_t)time(nullptr);
    return j.dump();
}

int64_t storedEpoch(const std::string& text) {
    try {
        return json::parse(text).value("readAtEpoch", (int64_t)0);
    } catch (...) {
        return 0;
    }
}

bool fromJson(const std::string& text, LcuRank& out) {
    if (text.empty()) return false;
    try {
        json j = json::parse(text);
        out.tier = j.value("tier", "");
        out.division = j.value("division", "");
        out.leaguePoints = j.value("leaguePoints", 0);
        out.wins = j.value("wins", 0);
        out.losses = j.value("losses", 0);
        out.queue = j.value("queue", std::string("RANKED_SOLO_5x5"));
        out.readAtIso = j.value("readAtIso", "");
        return !out.readAtIso.empty();
    } catch (...) {
        return false;
    }
}

} // namespace

RankView storedRankView(Db& db) {
    RankView v;
    if (!fromJson(db.getKv(kKey), v.rank)) {
        v.note = "Todavia no se ha leido tu rango. Abre League y vuelve a esta pantalla.";
        return v;
    }
    v.known = true;
    // Short on purpose: it shares a row with the rank itself. The long form
    // is kept for the cases that need an instruction, not a timestamp.
    v.note = "leido el " + v.rank.readAtIso + ", con League cerrado";
    return v;
}

RankView currentRankView(Db& db, Lcu* lcu, int maxAgeSec) {
    if (!lcu || !lcu->connected()) return storedRankView(db);

    std::string stored = db.getKv(kKey);
    int64_t age = (int64_t)time(nullptr) - storedEpoch(stored);
    LcuRank cached;
    if (maxAgeSec > 0 && storedEpoch(stored) > 0 && age < maxAgeSec && fromJson(stored, cached)) {
        RankView v;
        v.known = true;
        v.rank = cached;
        v.note = "leido hace " + std::to_string(age) + " s";
        return v;
    }

    auto fresh = lcu->currentRank();
    if (!fresh) {
        // The client answered nothing usable. What was stored is still the last
        // true reading, and it keeps its own date.
        RankView v = storedRankView(db);
        if (!v.known)
            v.note = "El cliente no devolvio tu rango. Entra a la pestana de "
                     "clasificatoria una vez y vuelve a intentarlo.";
        return v;
    }

    RankView v;
    v.known = true;
    v.live = true;
    v.rank = *fresh;
    db.setKv(kKey, toJson(*fresh));
    v.note = fresh->ranked() ? "leido de tu cliente ahora"
                             : "tu cliente dice que no tienes clasificacion en esta cola";
    return v;
}

} // namespace rl
