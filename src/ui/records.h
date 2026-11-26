#pragma once
// Vigil UI — the user's records outside the live session: saved profiles and the actions that
// create, share and bring them back (PLAN.md §3.4, X4). The detection layer owns the file format
// (src/detection/snapshot.h); this service adds what only the UI knows: the data folder, the
// identity summary, the clipboard, and a short message for the user after each action.
//
//   Save      Players → profile → ⋯ → Save profile            → Records → Saved
//   Export    the same menu, or Records → Saved → Export      → %LOCALAPPDATA%\Vigil\exports
//   Import    Records → Saved → Import: a file copied in Explorer (Ctrl+C), a path or profile
//             text on the clipboard, or files placed in %LOCALAPPDATA%\Vigil\imports

#include "../detection/engine.h"
#include "../detection/snapshot.h"
#include "app.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace vigil::ui {

struct ActionResult {
    bool ok = false;
    std::string message;    // one sentence for the user
    std::string id;         // saved-profile id when one was created
    std::string path;       // file written (export)
};

class RecordsService {
public:
    static RecordsService& instance();

    // Data folder (profiles/, exports/, imports/ under it). The preview harness uses a temp folder.
    void configure(const std::string& dataDir);
    bool configured() const { return !dataDir_.empty(); }
    detection::SnapshotStore& store() { return store_; }
    const detection::CheckCatalog& catalog(const detection::Engine& e);
    // Rescans the profiles folder when it may have changed (cheap enough to call when shown).
    void refreshIfStale(const detection::Engine& e, double nowSec);

    ActionResult save(const detection::Engine& e, const detection::PlayerRecord& r);
    ActionResult exportSaved(const std::string& id);
    // Saves a live profile, then exports it.
    ActionResult exportLive(const detection::Engine& e, const detection::PlayerRecord& r);
    ActionResult import(const detection::Engine& e, Host* host);
    ActionResult remove(const std::string& id);
    bool setNote(const detection::Engine& e, const std::string& id, const std::string& note);

    // A saved profile, loaded once and kept while it is being viewed. Null when unreadable
    // (`error` says why).
    const detection::ProfileSnapshot* loaded(const detection::Engine& e, const std::string& id, std::string& error);

    // The last action's message, shown briefly on the page that triggered it.
    void announce(const ActionResult& r, double nowSec);
    const ActionResult& lastResult() const { return last_; }
    double lastResultAt() const { return lastAt_; }

private:
    std::string dataDir_;
    detection::SnapshotStore store_;
    detection::CheckCatalog catalog_;
    bool catalogReady_ = false;
    double refreshedAt_ = -100.0;
    std::map<std::string, std::unique_ptr<detection::ProfileSnapshot>> cache_;
    ActionResult last_;
    double lastAt_ = -100.0;
};

// Clipboard contents that can be imported: file paths (a file copied in Explorer), a path typed
// or copied as text, or profile JSON copied as text.
struct ClipboardImport {
    std::vector<std::string> files;
    std::string text;
};
ClipboardImport readClipboardForImport();

}  // namespace vigil::ui
