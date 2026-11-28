//! `src/detection/replay.cpp`: an engine's state reduced to an outcome, and replaying frames.

use vigil_formats::outcome::{Outcome, PlayerOutcome};

use crate::engine::Engine;
use crate::types::WorldFrame;

/// `summarize`: per player (keyed by name; a later record with the same name wins, as in C++).
pub fn summarize(engine: &Engine, frames: i32) -> Outcome {
    let mut o = Outcome { frames, ..Outcome::default() };
    let checks = engine.checks();
    for r in engine.session().players() {
        let mut p = PlayerOutcome {
            name: r.name.clone(),
            suspicion: r.suspicion,
            peak: r.peak_suspicion,
            ..Default::default()
        };
        for (i, cr) in r.checks.iter().enumerate().take(checks.len()) {
            let id = checks[i].base.id.as_bytes().to_vec();
            if cr.evidence != 0 {
                p.evidence.insert(id.clone(), cr.evidence);
            }
            if cr.detections != 0 {
                p.detections.insert(id, cr.detections);
            }
        }
        o.players.insert(r.name.clone(), p);
    }
    o
}

/// `replayFrames`: a fresh session from the first frame's time, every frame, the outcome.
pub fn replay_frames(engine: &mut Engine, frames: &[WorldFrame]) -> Outcome {
    if let Some(first) = frames.first() {
        engine.reset_session(first.time_ms);
    }
    for f in frames {
        engine.on_frame(f);
    }
    summarize(engine, frames.len() as i32)
}
