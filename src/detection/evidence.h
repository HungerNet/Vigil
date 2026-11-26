#pragma once
// Vigil detection framework — evidence, detections and the confidence model.

#include "types.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace vigil::detection {

struct EvidenceValue {
    // Measure   a measured quantity (the first one with a limit is what the evidence is about)
    // Expected  what vanilla play produces in this player's context (the envelope's center)
    // Context   what shaped the expectation or the measurement (multiplier, ping, data quality)
    enum class Role : std::uint8_t { Measure, Expected, Context };
    std::string name;
    double value = 0.0;
    std::string unit;
    double limit = 0.0;     // the threshold it was compared against (0 = n/a)
    bool hasLimit = false;
    Role role = Role::Measure;
};

// The behavioral envelope behind one observation (PLAN.md §7.2): what legitimate play would have
// produced in this player's context, the edge of what legitimate play can produce, and what was
// observed. Checks describe it through their values; the engine derives it, so every piece of
// evidence answers "what was expected, what happened, and why is the difference suspicious?"
struct Envelope {
    std::string quantity;        // "Average speed"
    std::string unit;            // "b/tick"
    double observed = 0.0;
    double limit = 0.0;          // edge of the legitimate envelope
    double expected = 0.0;       // vanilla value in this context (valid when hasExpected)
    bool hasExpected = false;
    std::string expectedLabel;   // "Vanilla sprint-jump"
    bool upper = true;           // legitimate values are ≤ limit (false: ≥ limit, e.g. knockback taken)
    std::vector<std::string> context;   // "Movement multiplier 1.40x", "Victim ping 180 ms"

    bool valid() const { return !quantity.empty(); }
    // How far beyond the edge, relative to it (0.25 = 25 % beyond). 0 when inside.
    double deviation() const {
        if (!valid() || limit == 0.0) return 0.0;
        const double d = upper ? (observed - limit) / limit : (limit - observed) / limit;
        return d > 0.0 ? d : 0.0;
    }
};

// Builds the envelope from a check's values: the first limited Measure is the quantity, an
// Expected value is the center, Context values (and the remaining unlimited measures) are context.
Envelope deriveEnvelope(const std::vector<EvidenceValue>& values);

// A single observation that supports a detection. Explanations are generated at the moment of
// observation from the real measured numbers, so the UI never has to guess why.
struct Evidence {
    std::uint64_t id = 0;
    Tick tick = 0;
    double timeMs = 0.0;
    int check = -1;
    int indicator = -1;
    float weight = 1.f;          // indicator weight when emitted
    float severity = 0.f;        // 0..1 strength of this observation
    float contribution = 0.f;    // confidence points it added
    float confidenceAfter = 0.f;
    std::string summary;         // "Hit from 3.62 blocks"
    std::string detail;          // why it matters
    std::string target;          // other player involved, if any
    std::vector<EvidenceValue> values;
    Envelope envelope;           // derived from `values` when the evidence is recorded
};

struct Detection {
    std::uint64_t id = 0;
    Tick tick = 0;
    double timeMs = 0.0;
    int check = -1;
    float confidence = 0.f;
    int evidenceCount = 0;
    int distinctIndicators = 0;
    std::vector<std::uint64_t> evidence;  // ids of supporting evidence (most recent window)
    std::string summary;
};

struct ConfidenceParams {
    float alertThreshold = 70.f;   // 0..100
    float halfLifeSec = 30.f;      // decay half-life
    int minEvidence = 3;           // evidence items required inside the window
    int minIndicators = 1;         // distinct indicators required inside the window
    float windowSec = 60.f;        // evidence gate window
    float cooldownSec = 20.f;      // minimum time between detections
    float gain = 0.25f;            // fraction of remaining headroom one unit of evidence adds
};

// Evidence accumulates with diminishing returns and decays exponentially. A detection can only
// fire when the score crosses the threshold AND the evidence gate is satisfied, so a single
// outlier can never produce a flag on its own.
class ConfidenceModel {
public:
    float value() const { return value_; }
    void decayTo(double nowMs, float halfLifeSec);
    // Returns the contribution in points.
    float add(double nowMs, float weight, float severity, int indicator, const ConfidenceParams& p);
    void gate(double nowMs, const ConfidenceParams& p, int& count, int& distinct) const;
    bool canDetect(double nowMs, const ConfidenceParams& p) const;
    void markDetected(double nowMs) { lastDetectionMs_ = nowMs; }
    // A saved profile shows the value it had; it never evolves (snapshot.h).
    void restore(float value) { value_ = value < 0.f ? 0.f : value > 100.f ? 100.f : value; }
    double lastDetectionMs() const { return lastDetectionMs_; }
    void reset() { *this = ConfidenceModel{}; }
    // The player was out of view: nothing was observed, so nothing decays. Restarts the decay clock
    // at `nowMs` without charging the absence. `carry` < 1 (a new game) scales the value and empties
    // the evidence-gate window, so carried confidence can never complete a detection on its own.
    void resume(double nowMs, float carry = 1.f);

private:
    float value_ = 0.f;
    double lastMs_ = -1.0;
    double lastDetectionMs_ = -1e18;
    std::deque<std::pair<double, int>> recent_;
};

}  // namespace vigil::detection
