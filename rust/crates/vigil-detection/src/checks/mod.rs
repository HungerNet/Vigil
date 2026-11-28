//! `src/detection/checks/*`: the eleven checks, one module per C++ file, and their registry.

pub mod autoblock;
pub mod autoclicker;
pub mod bhop;
pub mod killaura;
pub mod noslow;
pub mod reach;
pub mod scaffold;
pub mod speed;
pub mod velocity;

use crate::check::Check;

/// `createAllChecks` (checks/registry.cpp): every check, in display order.
pub fn create_all_checks() -> Vec<Check> {
    vec![
        killaura::make(),
        reach::make(),
        autoclicker::make(),
        velocity::make(),
        autoblock::make(),
        speed::make(),
        bhop::make(),
        noslow::make(),
        scaffold::make_keep_y(),
        scaffold::make_flat(),
        scaffold::make_legit(),
    ]
}

/// What every check module uses.
pub(crate) mod prelude {
    pub use std::any::Any;
    pub use std::collections::VecDeque;

    pub use crate::check::{Check, CheckBase, CheckCategory, CheckImpl, Ctx, Registrar, state};
    pub use crate::combat::CombatEvent;
    pub use crate::config::{ConfigRead, ParamId, range};
    pub use crate::cpp;
    pub use crate::evidence::{EvidenceValue as V, Text};
    pub use crate::fmt;
    pub use crate::track::PlayerTrack;
    pub use crate::types::{BoxAabb, Tick, Vec3, mc, pos, wrap_degrees, yaw_towards};

    /// A target-less `std::string{}`.
    pub fn none() -> Text {
        Text::new()
    }
    /// A plain (non-`fmt`) string.
    pub fn text(s: &str) -> Text {
        s.as_bytes().to_vec()
    }
}
