#include "alerts.h"

#include "../core/json.h"

#include <cstdio>

namespace vigil::game {
namespace {

const char* severityColor(float confidence) {
    if (confidence >= 90.f) return "red";
    if (confidence >= 75.f) return "gold";
    return "yellow";
}

}  // namespace

std::string chatPrefix() { return "\xC2\xA7" "8[\xC2\xA7" "9Vigil\xC2\xA7" "8] \xC2\xA7" "r"; }

std::string buildAlertJson(const detection::Alert& alert, const std::string& checkName, bool clickToReport) {
    using json::Value;
    char pct[16];
    std::snprintf(pct, sizeof(pct), "%.0f%%", alert.confidence);

    Value hover = Value::object();
    hover["text"] = Value("");
    Value& hx = hover["extra"];
    hx = Value::array();
    auto add = [](Value& arr, const std::string& text, const char* color, bool bold = false) {
        Value v = Value::object();
        v["text"] = Value(text);
        v["color"] = Value(color);
        if (bold) v["bold"] = Value(true);
        arr.push(std::move(v));
    };
    add(hx, checkName, "blue", true);
    add(hx, "  " + std::string(pct) + " confidence\n", severityColor(alert.confidence));
    for (std::size_t i = 0; i < alert.reasons.size(); ++i) {
        add(hx, std::string(i == 0 ? "\n" : "") + "\xE2\x80\xA2 " + alert.reasons[i] + "\n", i == 0 ? "white" : "gray");
    }
    add(hx, clickToReport ? "\nClick to report with /wdr " + alert.name : "\nOpen /vigil for the full evidence", "dark_gray");

    Value root = Value::object();
    root["text"] = Value("");
    Value& ex = root["extra"];
    ex = Value::array();
    add(ex, "[", "dark_gray");
    add(ex, "Vigil", "blue");
    add(ex, "] ", "dark_gray");
    add(ex, alert.name, "white");
    add(ex, " \xC2\xB7 ", "dark_gray");
    add(ex, checkName + " ", "gray");
    add(ex, pct, severityColor(alert.confidence));
    Value he = Value::object();
    he["action"] = Value("show_text");
    he["value"] = hover;
    root["hoverEvent"] = he;
    if (clickToReport) {
        Value ce = Value::object();
        ce["action"] = Value("run_command");
        ce["value"] = Value("/wdr " + alert.name);
        root["clickEvent"] = ce;
    } else {
        Value ce = Value::object();
        ce["action"] = Value("run_command");
        ce["value"] = Value("/vigil " + alert.name);
        root["clickEvent"] = ce;
    }
    return json::write(root, -1);
}

std::string buildAlertLegacy(const detection::Alert& alert, const std::string& checkName) {
    char pct[16];
    std::snprintf(pct, sizeof(pct), "%.0f%%", alert.confidence);
    return chatPrefix() + "\xC2\xA7" "f" + alert.name + " \xC2\xA7" "8\xC2\xB7 \xC2\xA7" "7" + checkName + " \xC2\xA7" "e" + pct;
}

}  // namespace vigil::game
