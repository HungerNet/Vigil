//! `src/detection/roster.{h,cpp}`: who counts as a player.

use crate::evidence::Text;
use crate::types::PlayerFrame;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
#[repr(u8)]
pub enum EntityKind {
    #[default]
    Player = 0,
    Npc = 1,
    Duplicate = 2,
}

pub fn entity_kind_name(k: EntityKind) -> &'static str {
    match k {
        EntityKind::Npc => "NPC",
        EntityKind::Duplicate => "Duplicate",
        EntityKind::Player => "Player",
    }
}

/// Ticks an entity must be unlisted (and tracked) before the tab-list rule may apply.
pub const UNLISTED_GRACE_TICKS: i32 = 40;

/// Version nibble of a textual UUID (dashed or not); 0 when malformed.
pub fn uuid_version_of(uuid: &[u8]) -> i32 {
    let mut hex = Vec::with_capacity(32);
    for &c in uuid {
        if c == b'-' {
            continue;
        }
        if !c.is_ascii_hexdigit() {
            return 0;
        }
        hex.push(c);
    }
    if hex.len() != 32 {
        return 0;
    }
    let v = hex[12];
    if v.is_ascii_digit() { i32::from(v - b'0') } else { 0 }
}

/// 1–16 characters of `[A-Za-z0-9_]`.
pub fn is_valid_player_name(name: &[u8]) -> bool {
    if name.is_empty() || name.len() > 16 {
        return false;
    }
    name.iter().all(|&c| c.is_ascii_alphanumeric() || c == b'_')
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct EntityObservation {
    pub ticks_tracked: i32,
    pub ticks_unlisted: i32,
    pub max_displacement: f64,
    pub ever_invisible: bool,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct EntityVerdict {
    pub kind: EntityKind,
    pub reason: Text,
}

impl EntityVerdict {
    pub fn new(kind: EntityKind, reason: &str) -> Self {
        EntityVerdict { kind, reason: reason.into() }
    }
}

/// `std::tolower` in the "C" locale maps only A–Z, exactly as ASCII case folding does.
fn equals_ignore_case(a: &[u8], b: &[u8]) -> bool {
    a.eq_ignore_ascii_case(b)
}

pub fn classify_entity(f: &PlayerFrame, o: &EntityObservation, local_name: &[u8], replay: bool) -> EntityVerdict {
    if f.is_local {
        return EntityVerdict::default();
    }
    if uuid_version_of(&f.uuid) == 2 {
        return EntityVerdict::new(EntityKind::Npc, "Server NPC");
    }
    if !f.name.is_empty() && f.name[0] != b'#' && !is_valid_player_name(&f.name) {
        return EntityVerdict::new(EntityKind::Npc, "Not a player name");
    }
    if !local_name.is_empty() && equals_ignore_case(&f.name, local_name) {
        return EntityVerdict::new(EntityKind::Npc, "Uses your name");
    }
    if !replay
        && f.tab_known
        && o.ticks_tracked >= UNLISTED_GRACE_TICKS
        && o.ticks_unlisted >= UNLISTED_GRACE_TICKS
        && (o.max_displacement < 0.5 || o.ever_invisible)
    {
        return EntityVerdict::new(
            EntityKind::Npc,
            if o.ever_invisible { "Hidden bot (not in the player list)" } else { "Not in the player list" },
        );
    }
    EntityVerdict::default()
}
