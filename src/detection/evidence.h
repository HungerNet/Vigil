#pragma once
// Vigil detection framework — evidence, detections and the confidence model.

#include "types.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace vigil::detection {

struct EvidenceValue {
    std::string name;
    double value = 0.0;
    std::string unit;
    double limit = 0.0;     // the threshold it was compared against (0 = n/a)
    bool hasLimit = false;
};

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
    double lastDetectionMs() const { return lastDetectionMs_; }
    void reset() { *this = ConfidenceModel{}; }

private:
    float value_ = 0.f;
    double lastMs_ = -1.0;
    double lastDetectionMs_ = -1e18;
    std::deque<std::pair<double, int>> recent_;
};

}  // namespace vigil::detection
