// Writing one rune page to the League client (PRD 9.5 RF-RUN-003).
//
// Policy, and none of it is optional:
// - Behind Config::runeWriteEnabled, off by default. Kill switch (PRD 17.3).
// - RiftLoop manages exactly ONE page, the one whose name starts with the
//   managed prefix. It never reads, edits or deletes a personal page, and it
//   never frees a slot by removing one.
// - The user sees a diff and confirms. Nothing is written without that click.
// - One-click undo restores the previous content and the page that was
//   selected before.
// - If the user edits the managed page by hand, RiftLoop stops overwriting it
//   until the user says otherwise.
// - Every write is audited locally (PRD 17.4).
#pragma once
#include "core/db.h"
#include "core/ddragon.h"
#include "core/lcu.h"
#include "core/models.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace rl {

// The page name RiftLoop owns. Anything else belongs to the user.
inline constexpr const char* kManagedPagePrefix = "RiftLoop";

struct PerkPage {
    int64_t          id = 0;
    std::string      name;
    int              primaryStyleId = 0;
    int              subStyleId = 0;
    std::vector<int> selectedPerkIds;     // 6 perks + 3 stat shards
    bool             current = false;
    bool             isDeletable = true;
    bool             isEditable = true;
    bool             isTemporary = false;
    int64_t          lastModified = 0;

    bool managed() const { return name.rfind(kManagedPagePrefix, 0) == 0; }
};

struct PerkInventory {
    bool valid = false;
    bool canAddCustomPage = false;
    int  customPageCount = 0;
    int  ownedPageCount = 0;
};

// Pure parsers, exposed for tests. Malformed input yields an empty list or an
// invalid inventory; neither ever throws.
std::vector<PerkPage> parsePerkPages(const std::string& json);
PerkInventory         parsePerkInventory(const std::string& json);

// Content fingerprint. Two pages with the same styles and perks share it, so a
// change made in the client is detectable.
std::string pageSignature(int primaryStyle, int subStyle, const std::vector<int>& perks);
std::string pageSignature(const PerkPage& p);
std::string pageSignature(const RunePage& p);

enum class WriteAction { Create, Overwrite, NoChange, Blocked };

struct PageWritePlan {
    WriteAction              action = WriteAction::Blocked;
    int64_t                  targetPageId = 0;    // set for Overwrite
    std::string              pageName;
    std::vector<std::string> diff;                // one line per real change
    std::string              blockedReason;       // set when action == Blocked
    bool                     userEdited = false;  // managed page changed by hand
};

// Decides what a write would do, without doing it. lastAppliedSignature is what
// RiftLoop wrote last time (empty when it never wrote): a managed page that no
// longer matches it was edited by hand.
PageWritePlan planPageWrite(const std::vector<PerkPage>& pages, const PerkInventory& inv,
                            const RunePage& wanted, const std::string& pageName,
                            const std::string& lastAppliedSignature, const Ddragon& dd);

// Body the client expects to create or replace a page.
nlohmann::json pageBody(const RunePage& wanted, const std::string& name);

struct WriteResult {
    bool        ok = false;
    std::string message;             // shown to the user as-is
    int64_t     pageId = 0;
};

// Applies the page. force ignores a detected manual edit and must only be true
// after the user confirmed that specific case.
WriteResult applyRunePage(Db& db, Lcu& lcu, const Ddragon& dd, const RunePage& wanted,
                          const std::string& pageName, bool force);

// Restores what applyRunePage replaced: the previous content of the managed
// page (or removes the page RiftLoop created) and the page that was selected.
WriteResult undoRunePage(Db& db, Lcu& lcu);

// True when there is something to undo.
bool canUndoRunePage(Db& db);

} // namespace rl
