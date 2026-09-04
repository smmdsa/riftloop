#include "core/perkpages.h"
#include "core/util.h"

#include <algorithm>

namespace rl {

using nlohmann::json;

namespace {

// kv keys holding the undo state and the last thing RiftLoop wrote.
constexpr const char* kKeyLastSignature   = "rune_write_signature";
constexpr const char* kKeyUndoPrevPage    = "rune_undo_prev_page";     // "" = we created it
constexpr const char* kKeyUndoPageId      = "rune_undo_page_id";
constexpr const char* kKeyUndoPrevCurrent = "rune_undo_prev_current";

const char* slotLabel(int index) {
    switch (index) {
        case 0: return "Piedra angular";
        case 1: return "Primaria 1";
        case 2: return "Primaria 2";
        case 3: return "Primaria 3";
        case 4: return "Secundaria 1";
        case 5: return "Secundaria 2";
        case 6: return "Fragmento 1";
        case 7: return "Fragmento 2";
        case 8: return "Fragmento 3";
    }
    return "Slot";
}

std::string perkLabel(const Ddragon& dd, int index, int perkId) {
    if (perkId == 0) return "-";
    std::string name = index >= 6 ? dd.shardName(perkId) : dd.perkName(perkId);
    return name.empty() ? std::to_string(perkId) : name;
}

} // namespace

std::vector<PerkPage> parsePerkPages(const std::string& text) {
    std::vector<PerkPage> out;
    if (text.empty()) return out;
    try {
        json j = json::parse(text);
        if (!j.is_array()) return out;
        for (auto& p : j) {
            PerkPage page;
            page.id = p.value("id", (int64_t)0);
            page.name = p.value("name", "");
            page.primaryStyleId = p.value("primaryStyleId", 0);
            page.subStyleId = p.value("subStyleId", 0);
            for (auto& id : p.value("selectedPerkIds", json::array()))
                page.selectedPerkIds.push_back(id.get<int>());
            page.current = p.value("current", false);
            page.isDeletable = p.value("isDeletable", true);
            page.isEditable = p.value("isEditable", true);
            page.isTemporary = p.value("isTemporary", false);
            page.lastModified = p.value("lastModified", (int64_t)0);
            out.push_back(std::move(page));
        }
    } catch (...) {}
    return out;
}

PerkInventory parsePerkInventory(const std::string& text) {
    PerkInventory inv;
    if (text.empty()) return inv;
    try {
        json j = json::parse(text);
        inv.canAddCustomPage = j.value("canAddCustomPage", false);
        inv.customPageCount = j.value("customPageCount", 0);
        inv.ownedPageCount = j.value("ownedPageCount", 0);
        inv.valid = j.contains("ownedPageCount");
    } catch (...) {}
    return inv;
}

std::string pageSignature(int primaryStyle, int subStyle, const std::vector<int>& perks) {
    std::string s = std::to_string(primaryStyle) + ":" + std::to_string(subStyle);
    for (int p : perks) s += "," + std::to_string(p);
    return s;
}
std::string pageSignature(const PerkPage& p) {
    return pageSignature(p.primaryStyleId, p.subStyleId, p.selectedPerkIds);
}
std::string pageSignature(const RunePage& p) {
    return pageSignature(p.primaryStyle, p.subStyle, p.perks);
}

nlohmann::json pageBody(const RunePage& wanted, const std::string& name) {
    return json{{"name", name},
                {"primaryStyleId", wanted.primaryStyle},
                {"subStyleId", wanted.subStyle},
                {"selectedPerkIds", wanted.perks},
                {"current", true}};
}

PageWritePlan planPageWrite(const std::vector<PerkPage>& pages, const PerkInventory& inv,
                            const RunePage& wanted, const std::string& pageName,
                            const std::string& lastAppliedSignature, const Ddragon& dd) {
    PageWritePlan plan;
    plan.pageName = pageName;

    if (wanted.perks.size() != 9 || wanted.primaryStyle == 0 || wanted.subStyle == 0) {
        plan.blockedReason = "La pagina generada esta incompleta; no se escribe nada.";
        return plan;
    }

    const PerkPage* managed = nullptr;
    for (auto& p : pages)
        if (p.managed() && !p.isTemporary) { managed = &p; break; }

    if (!managed) {
        // No page of ours yet. Never free a slot by deleting a personal page.
        if (inv.valid && !inv.canAddCustomPage) {
            plan.blockedReason =
                "No queda espacio para otra pagina de runas (" +
                std::to_string(inv.customPageCount) + " de " +
                std::to_string(inv.ownedPageCount) +
                " usadas). Borra una tu mismo en el cliente: RiftLoop no borra paginas tuyas.";
            return plan;
        }
        plan.action = WriteAction::Create;
        for (size_t i = 0; i < wanted.perks.size(); ++i)
            plan.diff.push_back(std::string(slotLabel((int)i)) + ": " +
                                perkLabel(dd, (int)i, wanted.perks[i]));
        return plan;
    }

    if (!managed->isEditable) {
        plan.blockedReason = "El cliente marca esa pagina como no editable.";
        return plan;
    }

    std::string current = pageSignature(*managed);
    if (current == pageSignature(wanted)) {
        plan.action = WriteAction::NoChange;
        plan.targetPageId = managed->id;
        return plan;
    }

    // A managed page that no longer matches what we wrote was edited by hand.
    // RF-RUN-003: never silently overwrite the user's own edit.
    if (!lastAppliedSignature.empty() && current != lastAppliedSignature) {
        plan.userEdited = true;
        plan.targetPageId = managed->id;
        plan.blockedReason =
            "Editaste esa pagina a mano en el cliente. RiftLoop no la sobrescribe "
            "sin que lo confirmes.";
        return plan;
    }

    plan.action = WriteAction::Overwrite;
    plan.targetPageId = managed->id;
    if (managed->primaryStyleId != wanted.primaryStyle || managed->subStyleId != wanted.subStyle)
        plan.diff.push_back("Ramas: " + dd.styleName(managed->primaryStyleId) + " + " +
                            dd.styleName(managed->subStyleId) + "  ->  " +
                            dd.styleName(wanted.primaryStyle) + " + " +
                            dd.styleName(wanted.subStyle));
    for (size_t i = 0; i < wanted.perks.size(); ++i) {
        int before = i < managed->selectedPerkIds.size() ? managed->selectedPerkIds[i] : 0;
        if (before == wanted.perks[i]) continue;
        plan.diff.push_back(std::string(slotLabel((int)i)) + ": " +
                            perkLabel(dd, (int)i, before) + "  ->  " +
                            perkLabel(dd, (int)i, wanted.perks[i]));
    }
    return plan;
}

WriteResult applyRunePage(Db& db, Lcu& lcu, const Ddragon& dd, const RunePage& wanted,
                          const std::string& pageName, bool force) {
    WriteResult res;
    if (!lcu.connected()) {
        res.message = "El cliente de League no esta abierto.";
        return res;
    }
    auto pages = parsePerkPages(lcu.getRaw("/lol-perks/v1/pages"));
    auto inv = parsePerkInventory(lcu.getRaw("/lol-perks/v1/inventory"));
    std::string lastSig = db.getKv(kKeyLastSignature);
    PageWritePlan plan = planPageWrite(pages, inv, wanted, pageName, lastSig, dd);

    if (plan.action == WriteAction::NoChange) {
        res.ok = true;
        res.pageId = plan.targetPageId;
        res.message = "La pagina ya estaba aplicada; no se toco nada.";
        return res;
    }
    if (plan.action == WriteAction::Blocked && !(plan.userEdited && force)) {
        res.message = plan.blockedReason;
        return res;
    }

    // Remember what to restore before changing anything.
    std::string prevCurrent;
    for (auto& p : pages)
        if (p.current) prevCurrent = std::to_string(p.id);
    const PerkPage* managed = nullptr;
    for (auto& p : pages)
        if (p.managed() && !p.isTemporary) { managed = &p; break; }
    json prev;
    if (managed)
        prev = json{{"name", managed->name},
                    {"primaryStyleId", managed->primaryStyleId},
                    {"subStyleId", managed->subStyleId},
                    {"selectedPerkIds", managed->selectedPerkIds}};

    std::string body = pageBody(wanted, pageName).dump();
    std::string response;
    bool ok = false;
    int64_t pageId = managed ? managed->id : 0;

    if (managed) {
        ok = lcu.requestRaw("PUT", "/lol-perks/v1/pages/" + std::to_string(managed->id), body,
                            &response);
        if (!ok) {
            // Some client builds refuse PUT on a page. Replacing our own page
            // is still allowed; a personal page is never touched.
            lcu.requestRaw("DELETE", "/lol-perks/v1/pages/" + std::to_string(managed->id), "");
            ok = lcu.postRaw("/lol-perks/v1/pages", body, &response);
            pageId = 0;
        }
    } else {
        ok = lcu.postRaw("/lol-perks/v1/pages", body, &response);
    }
    if (!ok) {
        db.audit("rune_write_failed", json{{"page", pageName}, {"response", response}}.dump());
        res.message = "El cliente rechazo la escritura. No se cambio ninguna pagina tuya.";
        return res;
    }
    if (pageId == 0) {
        try {
            pageId = json::parse(response).value("id", (int64_t)0);
        } catch (...) {}
    }
    if (pageId != 0)
        lcu.requestRaw("PUT", "/lol-perks/v1/currentpage", std::to_string(pageId));

    db.setKv(kKeyLastSignature, pageSignature(wanted));
    db.setKv(kKeyUndoPrevPage, prev.is_null() ? "" : prev.dump());
    db.setKv(kKeyUndoPageId, std::to_string(pageId));
    db.setKv(kKeyUndoPrevCurrent, prevCurrent);
    // PRD 17.4: moment, state, action, reason, consent, result, versions.
    db.audit("rune_write",
             json{{"page", pageName},
                  {"action", plan.action == WriteAction::Create ? "create" : "overwrite"},
                  {"forced_over_manual_edit", plan.userEdited && force},
                  {"page_id", pageId},
                  {"signature", pageSignature(wanted)},
                  {"patch", dd.version()},
                  {"changes", plan.diff}}
                 .dump());

    res.ok = true;
    res.pageId = pageId;
    res.message = plan.action == WriteAction::Create
                      ? "Pagina creada y seleccionada en el cliente."
                      : "Pagina actualizada y seleccionada en el cliente.";
    return res;
}

bool canUndoRunePage(Db& db) { return !db.getKv(kKeyUndoPageId).empty(); }

WriteResult undoRunePage(Db& db, Lcu& lcu) {
    WriteResult res;
    if (!lcu.connected()) {
        res.message = "El cliente de League no esta abierto.";
        return res;
    }
    std::string idText = db.getKv(kKeyUndoPageId);
    if (idText.empty()) {
        res.message = "No hay nada que deshacer.";
        return res;
    }
    std::string prev = db.getKv(kKeyUndoPrevPage);
    bool ok = false;
    if (prev.empty()) {
        // RiftLoop created the page: removing it removes our own, never yours.
        ok = lcu.requestRaw("DELETE", "/lol-perks/v1/pages/" + idText, "");
    } else {
        ok = lcu.requestRaw("PUT", "/lol-perks/v1/pages/" + idText, prev);
    }
    std::string prevCurrent = db.getKv(kKeyUndoPrevCurrent);
    if (!prevCurrent.empty())
        lcu.requestRaw("PUT", "/lol-perks/v1/currentpage", prevCurrent);

    db.audit("rune_write_undo",
             json{{"page_id", idText}, {"restored", !prev.empty()}, {"result", ok}}.dump());
    if (ok) {
        db.setKv(kKeyUndoPageId, "");
        db.setKv(kKeyUndoPrevPage, "");
        db.setKv(kKeyLastSignature, "");
        res.ok = true;
        res.message = prev.empty() ? "Pagina creada por RiftLoop eliminada."
                                   : "Pagina restaurada como estaba.";
    } else {
        res.message = "El cliente rechazo deshacer. Revisa tus paginas en el cliente.";
    }
    return res;
}

} // namespace rl
