#include "records.h"

#include "../identity/identity.h"
#include "sound.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace vigil::ui {

RecordsService& RecordsService::instance() {
    static RecordsService s;
    return s;
}

void RecordsService::configure(const std::string& dataDir) {
    dataDir_ = dataDir;
    const fs::path d(dataDir);
    store_.setDirectories((d / "profiles").string(), (d / "exports").string(), (d / "imports").string());
    refreshedAt_ = -100.0;
    cache_.clear();
}

const detection::CheckCatalog& RecordsService::catalog(const detection::Engine& e) {
    if (!catalogReady_) {
        catalog_ = detection::CheckCatalog::of(e);
        catalogReady_ = true;
    }
    return catalog_;
}

void RecordsService::refreshIfStale(const detection::Engine& e, double nowSec) {
    if (!configured() || nowSec - refreshedAt_ < 3.0) return;
    refreshedAt_ = nowSec;
    store_.refresh(catalog(e));
}

ActionResult RecordsService::save(const detection::Engine& e, const detection::PlayerRecord& r) {
    ActionResult res;
    if (!configured()) { res.message = "Profiles can't be saved here."; return res; }
    detection::ProfileSnapshot s = detection::captureSnapshot(e, r);
    identity::Profile ip;
    if (identity::Service::instance().find(r.key, ip) && ip.nicked) {
        s.nicked = true;
        s.likelyAccount = ip.realName;
        s.candidates = ip.candidates;
    }
    std::string err;
    if (!store_.save(s, catalog(e), res.id, err)) {
        res.message = err;
        return res;
    }
    res.ok = true;
    res.message = "Saved " + r.name + "'s profile to Records.";
    return res;
}

ActionResult RecordsService::exportSaved(const std::string& id) {
    ActionResult res;
    std::string err;
    if (!store_.exportTo(id, res.path, err)) {
        res.message = err;
        return res;
    }
    res.ok = true;
    res.id = id;
    res.message = "Exported to " + fs::path(res.path).filename().string() + " in the exports folder.";
    return res;
}

ActionResult RecordsService::exportLive(const detection::Engine& e, const detection::PlayerRecord& r) {
    ActionResult saved = save(e, r);
    if (!saved.ok) return saved;
    ActionResult res = exportSaved(saved.id);
    if (res.ok) res.message = "Saved and exported " + r.name + "'s profile (" + fs::path(res.path).filename().string() + ").";
    return res;
}

ClipboardImport readClipboardForImport() {
    ClipboardImport out;
    if (!OpenClipboard(nullptr)) return out;
    if (HANDLE h = GetClipboardData(CF_HDROP)) {
        if (auto drop = static_cast<HDROP>(GlobalLock(h))) {
            const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < n && i < 32; ++i) {
                wchar_t path[MAX_PATH] = {};
                if (DragQueryFileW(drop, i, path, MAX_PATH)) {
                    // The rest of Vigil opens files through narrow (ANSI) paths; convert without
                    // throwing on characters the code page lacks (such a file then fails to open).
                    char narrow[MAX_PATH * 2] = {};
                    WideCharToMultiByte(CP_ACP, 0, path, -1, narrow, sizeof(narrow), nullptr, nullptr);
                    out.files.push_back(narrow);
                }
            }
            GlobalUnlock(h);
        }
    }
    if (out.files.empty()) {
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
            if (const auto* w = static_cast<const wchar_t*>(GlobalLock(h))) {
                const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
                if (n > 1 && n < 9 * 1024 * 1024) {
                    out.text.resize(static_cast<std::size_t>(n - 1));
                    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.text.data(), n, nullptr, nullptr);
                }
                GlobalUnlock(h);
            }
        }
    }
    CloseClipboard();
    return out;
}

ActionResult RecordsService::import(const detection::Engine& e, Host* host) {
    ActionResult res;
    if (!configured()) { res.message = "Profiles can't be imported here."; return res; }
    const auto& cat = catalog(e);
    int added = 0;
    std::vector<std::string> errors;
    // 1. A file (or several) copied in Explorer, or a path / profile text on the clipboard.
    ClipboardImport clip = readClipboardForImport();
    std::string text = clip.text;
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') text = text.substr(1, text.size() - 2);
    std::error_code ec;
    if (!text.empty() && text.front() != '{' && text.size() < MAX_PATH && fs::is_regular_file(text, ec)) clip.files.push_back(text);
    for (const auto& f : clip.files) {
        if (fs::path(f).extension() != ".json") { errors.push_back(fs::path(f).filename().string() + ": not a profile file"); continue; }
        std::string id, err;
        if (store_.importFile(f, cat, id, err)) { ++added; res.id = id; }
        else errors.push_back(fs::path(f).filename().string() + ": " + err);
    }
    if (clip.files.empty() && !text.empty() && text.front() == '{') {
        // Profile JSON copied as text: validated through the same path as a file.
        const fs::path tmp = fs::path(store_.importsDir()) / "clipboard-import.tmp.json";
        fs::create_directories(tmp.parent_path(), ec);
        { std::ofstream o(tmp, std::ios::binary | std::ios::trunc); o << text; }
        std::string id, err;
        if (store_.importFile(tmp.string(), cat, id, err)) { ++added; res.id = id; }
        else errors.push_back("clipboard: " + err);
        fs::remove(tmp, ec);
    }
    // 2. Files waiting in the imports folder.
    added += store_.importPending(cat, errors);
    refreshedAt_ = -100.0;
    if (added > 0) {
        res.ok = true;
        res.message = "Imported " + std::to_string(added) + (added == 1 ? " profile" : " profiles") +
                      (errors.empty() ? "." : "; " + std::to_string(errors.size()) + " could not be read.");
        if (added > 1) res.id.clear();
        return res;
    }
    if (!errors.empty()) {
        res.message = errors.front();
        return res;
    }
    // Nothing to import: show where profiles go.
    fs::create_directories(store_.importsDir(), ec);
    if (host) host->openFolder(store_.importsDir());
    res.message = "Copy a profile file in Explorer (Ctrl+C), or place it in the imports folder that just opened, then Import again.";
    return res;
}

ActionResult RecordsService::remove(const std::string& id) {
    ActionResult res;
    res.ok = store_.remove(id);
    cache_.erase(id);
    res.message = res.ok ? "Deleted the saved profile." : "The saved profile could not be deleted.";
    return res;
}

bool RecordsService::setNote(const detection::Engine& e, const std::string& id, const std::string& note) {
    const bool ok = store_.setNote(id, note, catalog(e));
    if (ok) {
        auto it = cache_.find(id);
        if (it != cache_.end()) it->second->note = note;
    }
    return ok;
}

const detection::ProfileSnapshot* RecordsService::loaded(const detection::Engine& e, const std::string& id, std::string& error) {
    auto it = cache_.find(id);
    if (it != cache_.end()) return it->second.get();
    auto s = std::make_unique<detection::ProfileSnapshot>();
    if (!store_.load(id, catalog(e), *s, error)) return nullptr;
    if (cache_.size() > 8) cache_.clear();
    return (cache_[id] = std::move(s)).get();
}

void RecordsService::announce(const ActionResult& r, double nowSec) {
    last_ = r;
    lastAt_ = nowSec;
    playSound(r.ok ? Sound::Saved : Sound::Error);
}

}  // namespace vigil::ui
