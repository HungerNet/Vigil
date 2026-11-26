#include "evidence.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace vigil::detection {

void ConfidenceModel::decayTo(double nowMs, float halfLifeSec) {
    if (lastMs_ < 0.0) { lastMs_ = nowMs; return; }
    const double dt = (nowMs - lastMs_) / 1000.0;
    lastMs_ = nowMs;
    if (dt <= 0.0 || value_ <= 0.f) return;
    const double hl = std::max(1.0, static_cast<double>(halfLifeSec));
    value_ = static_cast<float>(value_ * std::pow(0.5, dt / hl));
    if (value_ < 0.05f) value_ = 0.f;
}

void ConfidenceModel::resume(double nowMs, float carry) {
    lastMs_ = nowMs;
    if (carry < 1.f) {
        value_ *= std::clamp(carry, 0.f, 1.f);
        if (value_ < 0.05f) value_ = 0.f;
        recent_.clear();
    }
}

float ConfidenceModel::add(double nowMs, float weight, float severity, int indicator, const ConfidenceParams& p) {
    decayTo(nowMs, p.halfLifeSec);
    const float strength = std::clamp(weight, 0.f, 4.f) * std::clamp(severity, 0.f, 1.f);
    const float headroom = 100.f - value_;
    // Repeated application of the same fraction gives diminishing returns toward 100.
    const float fraction = 1.f - std::pow(1.f - std::clamp(p.gain, 0.01f, 0.9f), strength);
    const float points = headroom * fraction;
    value_ = std::min(100.f, value_ + points);
    recent_.emplace_back(nowMs, indicator);
    const double windowMs = std::max(1.f, p.windowSec) * 1000.0;
    while (!recent_.empty() && nowMs - recent_.front().first > windowMs) recent_.pop_front();
    while (recent_.size() > 256) recent_.pop_front();
    return points;
}

void ConfidenceModel::gate(double nowMs, const ConfidenceParams& p, int& count, int& distinct) const {
    const double windowMs = std::max(1.f, p.windowSec) * 1000.0;
    std::set<int> kinds;
    count = 0;
    for (const auto& e : recent_) {
        if (nowMs - e.first > windowMs) continue;
        ++count;
        kinds.insert(e.second);
    }
    distinct = static_cast<int>(kinds.size());
}

Envelope deriveEnvelope(const std::vector<EvidenceValue>& values) {
    Envelope e;
    const EvidenceValue* measure = nullptr;
    for (const auto& v : values) {
        if (v.role == EvidenceValue::Role::Measure && v.hasLimit) { measure = &v; break; }
    }
    if (!measure) return e;
    e.quantity = measure->name;
    e.unit = measure->unit;
    e.observed = measure->value;
    e.limit = measure->limit;
    for (const auto& v : values) {
        if (&v == measure) continue;
        if (v.role == EvidenceValue::Role::Expected && !e.hasExpected) {
            e.expected = v.value;
            e.hasExpected = true;
            e.expectedLabel = v.name;
            continue;
        }
        if (v.hasLimit) continue;   // a second limited measure is its own observation, not context
        char buf[96];
        const double a = std::fabs(v.value);
        std::snprintf(buf, sizeof(buf), a >= 100.0 ? "%.0f" : a >= 10.0 ? "%.1f" : "%.2f", v.value);
        std::string text = v.name + " " + buf;
        if (!v.unit.empty()) text += (v.unit == "%" || v.unit == "x" || v.unit == "\xC2\xB0") ? v.unit : " " + v.unit;
        e.context.push_back(text);
    }
    // Which side is legitimate: an expectation above the limit means legitimate play produces
    // more than the limit (knockback taken, air time); otherwise less (speed, reach).
    if (e.hasExpected) e.upper = e.expected <= e.limit;
    else e.upper = e.observed >= e.limit;
    return e;
}

bool ConfidenceModel::canDetect(double nowMs, const ConfidenceParams& p) const {
    if (value_ < p.alertThreshold) return false;
    if (nowMs - lastDetectionMs_ < p.cooldownSec * 1000.0) return false;
    int count = 0, distinct = 0;
    gate(nowMs, p, count, distinct);
    return count >= p.minEvidence && distinct >= p.minIndicators;
}

}  // namespace vigil::detection
