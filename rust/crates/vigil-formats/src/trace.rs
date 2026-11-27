//! `vigil-trace` format 1 — a mirror of `src/detection/trace.{h,cpp}`.
//!
//! JSON lines: line 1 is the header, each further line one `WorldFrame`. Player fields use short
//! keys and are omitted at their default value; the 23 boolean fields travel as bits of `f`.

use crate::json::{self, Text, Value};
use crate::{to_f32, to_i32};

pub const TRACE_FORMAT: i32 = 1;

#[derive(Clone, Debug, PartialEq)]
pub struct TraceHeader {
    pub format: i32,
    pub vigil: Text,
    pub build: Text,
    pub started_epoch_ms: f64,
    pub note: Text,
}

impl Default for TraceHeader {
    fn default() -> Self {
        TraceHeader {
            format: TRACE_FORMAT,
            vigil: Text::new(),
            build: Text::new(),
            started_epoch_ms: 0.0,
            note: Text::new(),
        }
    }
}

/// `detection::GameContext`.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
#[repr(u8)]
pub enum GameContext {
    #[default]
    Unknown = 0,
    Lobby = 1,
    Pregame = 2,
    InGame = 3,
    Replay = 4,
}

impl GameContext {
    fn from_i32(v: i32) -> GameContext {
        match v {
            1 => GameContext::Lobby,
            2 => GameContext::Pregame,
            3 => GameContext::InGame,
            4 => GameContext::Replay,
            _ => GameContext::Unknown,
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct Potions {
    pub speed: i32,
    pub slowness: i32,
    pub jump_boost: i32,
    pub count: i32,
}

impl Default for Potions {
    fn default() -> Self {
        Potions { speed: -1, slowness: -1, jump_boost: -1, count: 0 }
    }
}

/// `detection::PlayerFrame`.
#[derive(Clone, Debug, PartialEq)]
pub struct PlayerFrame {
    pub entity_id: i32,
    pub uuid: Text,
    pub name: Text,
    pub is_local: bool,
    pub is_teammate: bool,
    pub pos: [f64; 3],
    pub server_position: bool,
    pub yaw: f32,
    pub pitch: f32,
    pub on_ground: bool,
    pub sprinting: bool,
    pub sneaking: bool,
    pub using_item: bool,
    pub blocking: bool,
    pub invisible: bool,
    pub riding: bool,
    pub in_water: bool,
    pub in_lava: bool,
    pub on_ladder: bool,
    pub in_web: bool,
    pub dead: bool,
    pub held_item: i32,
    pub swing_progress: i32,
    pub swing_in_progress: bool,
    pub hurt_time: i32,
    pub health: f32,
    pub potions: Potions,
    pub potions_known: bool,
    pub potion_particles: bool,
    pub move_speed_attribute: f64,
    pub sprint_modifier: bool,
    pub block_below: i32,
    pub block_above_head: i32,
    pub wall_near: bool,
    pub wall_mask: u8,
    pub burning: bool,
    pub tab_known: bool,
    pub tab_listed: bool,
    pub ping_ms: i32,
}

impl Default for PlayerFrame {
    fn default() -> Self {
        PlayerFrame {
            entity_id: -1,
            uuid: Text::new(),
            name: Text::new(),
            is_local: false,
            is_teammate: false,
            pos: [0.0; 3],
            server_position: false,
            yaw: 0.0,
            pitch: 0.0,
            on_ground: false,
            sprinting: false,
            sneaking: false,
            using_item: false,
            blocking: false,
            invisible: false,
            riding: false,
            in_water: false,
            in_lava: false,
            on_ladder: false,
            in_web: false,
            dead: false,
            held_item: -1,
            swing_progress: 0,
            swing_in_progress: false,
            hurt_time: 0,
            health: -1.0,
            potions: Potions::default(),
            potions_known: false,
            potion_particles: false,
            move_speed_attribute: -1.0,
            sprint_modifier: false,
            block_below: -1,
            block_above_head: -1,
            wall_near: false,
            wall_mask: 0,
            burning: false,
            tab_known: false,
            tab_listed: false,
            ping_ms: -1,
        }
    }
}

/// `detection::WorldFrame`.
#[derive(Clone, Debug, PartialEq, Default)]
pub struct WorldFrame {
    pub tick: i64,
    pub time_ms: f64,
    pub skipped_ticks: i32,
    pub context: GameContext,
    pub context_detail: Text,
    pub players: Vec<PlayerFrame>,
    pub chat: Vec<Text>,
    pub world_changed: bool,
}

// Flag bits of "f" (trace.cpp), in bit order.
const FLAG_COUNT: usize = 23;

fn flags_of(p: &PlayerFrame) -> u32 {
    let bits = [
        p.on_ground,
        p.sprinting,
        p.sneaking,
        p.using_item,
        p.blocking,
        p.invisible,
        p.riding,
        p.in_water,
        p.in_lava,
        p.on_ladder,
        p.in_web,
        p.dead,
        p.swing_in_progress,
        p.potions_known,
        p.potion_particles,
        p.sprint_modifier,
        p.burning,
        p.tab_known,
        p.tab_listed,
        p.wall_near,
        p.server_position,
        p.is_local,
        p.is_teammate,
    ];
    debug_assert_eq!(bits.len(), FLAG_COUNT);
    bits.iter().enumerate().fold(0u32, |f, (i, &b)| if b { f | (1 << i) } else { f })
}

fn apply_flags(p: &mut PlayerFrame, f: u32) {
    let bit = |i: u32| f & (1 << i) != 0;
    p.on_ground = bit(0);
    p.sprinting = bit(1);
    p.sneaking = bit(2);
    p.using_item = bit(3);
    p.blocking = bit(4);
    p.invisible = bit(5);
    p.riding = bit(6);
    p.in_water = bit(7);
    p.in_lava = bit(8);
    p.on_ladder = bit(9);
    p.in_web = bit(10);
    p.dead = bit(11);
    p.swing_in_progress = bit(12);
    p.potions_known = bit(13);
    p.potion_particles = bit(14);
    p.sprint_modifier = bit(15);
    p.burning = bit(16);
    p.tab_known = bit(17);
    p.tab_listed = bit(18);
    p.wall_near = bit(19);
    p.server_position = bit(20);
    p.is_local = bit(21);
    p.is_teammate = bit(22);
}

fn player_json(p: &PlayerFrame) -> Value {
    let d = PlayerFrame::default();
    let mut j = Value::object();
    j.set("e", Value::from(p.entity_id));
    if !p.uuid.is_empty() {
        j.set("u", Value::from(&p.uuid));
    }
    if !p.name.is_empty() {
        j.set("n", Value::from(&p.name));
    }
    j.set("p", Value::Array(p.pos.iter().map(|&x| Value::Number(x)).collect()));
    j.set("yw", Value::Number(f64::from(p.yaw)));
    j.set("pt", Value::Number(f64::from(p.pitch)));
    j.set("f", Value::Number(f64::from(flags_of(p))));
    if p.held_item != d.held_item {
        j.set("h", Value::from(p.held_item));
    }
    if p.swing_progress != d.swing_progress {
        j.set("sw", Value::from(p.swing_progress));
    }
    if p.hurt_time != d.hurt_time {
        j.set("hu", Value::from(p.hurt_time));
    }
    if p.health != d.health {
        j.set("hp", Value::Number(f64::from(p.health)));
    }
    if p.potions != d.potions {
        let po = &p.potions;
        j.set("po", Value::Array(vec![po.speed.into(), po.slowness.into(), po.jump_boost.into(), po.count.into()]));
    }
    if p.move_speed_attribute != d.move_speed_attribute {
        j.set("ms", Value::Number(p.move_speed_attribute));
    }
    if p.block_below != d.block_below {
        j.set("bb", Value::from(p.block_below));
    }
    if p.block_above_head != d.block_above_head {
        j.set("ba", Value::from(p.block_above_head));
    }
    if p.wall_mask != 0 {
        j.set("wm", Value::from(i32::from(p.wall_mask)));
    }
    if p.ping_ms != d.ping_ms {
        j.set("pg", Value::from(p.ping_ms));
    }
    j
}

fn player_from(j: &Value) -> Option<PlayerFrame> {
    if !j.is_object() {
        return None;
    }
    let mut p = PlayerFrame { entity_id: to_i32(j.get("e").as_number_or(-1.0)), ..PlayerFrame::default() };
    p.uuid = j.get("u").as_text();
    p.name = j.get("n").as_text();
    let pos = j.get("p");
    if !pos.is_array() || pos.size() != 3 {
        return None;
    }
    p.pos = [pos.at(0).as_number(), pos.at(1).as_number(), pos.at(2).as_number()];
    p.yaw = to_f32(j.get("yw").as_number());
    p.pitch = to_f32(j.get("pt").as_number());
    apply_flags(&mut p, j.get("f").as_number() as u32);
    p.held_item = to_i32(j.get("h").as_number_or(-1.0));
    p.swing_progress = to_i32(j.get("sw").as_number_or(0.0));
    p.hurt_time = to_i32(j.get("hu").as_number_or(0.0));
    p.health = to_f32(j.get("hp").as_number_or(-1.0));
    if let Some(po) = j.find("po").filter(|po| po.is_array() && po.size() == 4) {
        p.potions = Potions {
            speed: to_i32(po.at(0).as_number_or(-1.0)),
            slowness: to_i32(po.at(1).as_number_or(-1.0)),
            jump_boost: to_i32(po.at(2).as_number_or(-1.0)),
            count: to_i32(po.at(3).as_number_or(0.0)),
        };
    }
    p.move_speed_attribute = j.get("ms").as_number_or(-1.0);
    p.block_below = to_i32(j.get("bb").as_number_or(-1.0));
    p.block_above_head = to_i32(j.get("ba").as_number_or(-1.0));
    p.wall_mask = j.get("wm").as_number_or(0.0) as u8;
    p.ping_ms = to_i32(j.get("pg").as_number_or(-1.0));
    Some(p)
}

/// `traceHeaderLine` (without the newline).
pub fn header_line(h: &TraceHeader) -> Vec<u8> {
    let mut j = Value::object();
    j.set("kind", Value::from("vigil-trace"));
    j.set("format", Value::from(h.format));
    j.set("vigil", Value::from(&h.vigil));
    j.set("build", Value::from(&h.build));
    j.set("started", Value::Number(h.started_epoch_ms));
    if !h.note.is_empty() {
        j.set("note", Value::from(&h.note));
    }
    json::write(&j, -1)
}

/// `parseTraceHeader`.
pub fn parse_header(line: &[u8]) -> Result<TraceHeader, String> {
    let j = json::parse(line).map_err(|e| e.0.to_string())?;
    if j.get("kind").as_bytes() != b"vigil-trace" {
        return Err("not a Vigil trace".into());
    }
    let format = to_i32(j.get("format").as_number_or(0.0));
    if !(1..=TRACE_FORMAT).contains(&format) {
        return Err(format!("unsupported trace format {format}"));
    }
    Ok(TraceHeader {
        format,
        vigil: j.get("vigil").as_text(),
        build: j.get("build").as_text(),
        started_epoch_ms: j.get("started").as_number(),
        note: j.get("note").as_text(),
    })
}

/// `traceFrameLine` (without the newline).
pub fn frame_line(f: &WorldFrame) -> Vec<u8> {
    let mut j = Value::object();
    j.set("t", Value::Number(f.tick as f64));
    j.set("ms", Value::Number(f.time_ms));
    if f.skipped_ticks != 0 {
        j.set("sk", Value::from(f.skipped_ticks));
    }
    j.set("c", Value::from(i32::from(f.context as u8)));
    if !f.context_detail.is_empty() {
        j.set("cd", Value::from(&f.context_detail));
    }
    if f.world_changed {
        j.set("wc", Value::Bool(true));
    }
    if !f.chat.is_empty() {
        j.set("chat", Value::Array(f.chat.iter().map(Value::from).collect()));
    }
    j.set("pl", Value::Array(f.players.iter().map(player_json).collect()));
    json::write(&j, -1)
}

/// `parseTraceFrame`.
pub fn parse_frame(line: &[u8]) -> Result<WorldFrame, String> {
    let j = json::parse(line).map_err(|e| e.0.to_string())?;
    if !j.is_object() {
        return Err("malformed frame".into());
    }
    let mut f = WorldFrame {
        tick: j.get("t").as_number() as i64,
        time_ms: j.get("ms").as_number(),
        skipped_ticks: to_i32(j.get("sk").as_number_or(0.0)),
        context: GameContext::from_i32(to_i32(j.get("c").as_number_or(0.0))),
        context_detail: j.get("cd").as_text(),
        world_changed: j.get("wc").as_bool(),
        ..WorldFrame::default()
    };
    f.chat = j.get("chat").items().iter().map(Value::as_text).collect();
    for pj in j.get("pl").items() {
        f.players.push(player_from(pj).ok_or_else(|| "malformed player".to_string())?);
    }
    Ok(f)
}

/// A whole trace file.
#[derive(Clone, Debug, PartialEq, Default)]
pub struct Trace {
    pub header: TraceHeader,
    pub frames: Vec<WorldFrame>,
}

/// Lines as `std::getline` yields them: split on `\n`, no empty line after a final `\n`.
fn getlines(bytes: &[u8]) -> impl Iterator<Item = &[u8]> {
    let mut parts: Vec<&[u8]> = bytes.split(|&b| b == b'\n').collect();
    if bytes.is_empty() || bytes.last() == Some(&b'\n') {
        parts.pop();
    }
    parts.into_iter()
}

fn strip_cr(line: &[u8]) -> &[u8] {
    line.strip_suffix(b"\r").unwrap_or(line)
}

/// `readTrace` on the file's bytes.
pub fn read(bytes: &[u8]) -> Result<Trace, String> {
    let mut lines = getlines(bytes);
    let first = lines.next().ok_or_else(|| "empty trace".to_string())?;
    let header = parse_header(strip_cr(first))?;
    let mut frames = Vec::new();
    for (n, line) in (2..).zip(lines) {
        let line = strip_cr(line);
        if line.is_empty() {
            continue;
        }
        frames.push(parse_frame(line).map_err(|e| format!("line {n}: {e}"))?);
    }
    Ok(Trace { header, frames })
}

/// The file `TraceWriter` (or `VigilReplay --synthesize`) writes for these frames.
pub fn write(t: &Trace) -> Vec<u8> {
    let mut out = header_line(&t.header);
    out.push(b'\n');
    for f in &t.frames {
        out.extend(frame_line(f));
        out.push(b'\n');
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_field_round_trips() {
        let p = PlayerFrame {
            entity_id: 7,
            uuid: b"00000000-0000-4000-8000-000000000007".to_vec(),
            name: b"Someone".to_vec(),
            pos: [1.03125, 64.0, -3.5],
            yaw: 12.65625,
            pitch: -3.5,
            on_ground: true,
            is_teammate: true,
            held_item: 276,
            swing_progress: 3,
            swing_in_progress: true,
            hurt_time: 9,
            health: 17.5,
            potions: Potions { speed: 1, slowness: -1, jump_boost: 0, count: 2 },
            move_speed_attribute: 0.13,
            block_below: 2,
            block_above_head: 0,
            wall_mask: 0b1010_0001,
            ping_ms: 85,
            ..PlayerFrame::default()
        };
        let f = WorldFrame {
            tick: 1234,
            time_ms: 61700.0,
            skipped_ticks: 2,
            context: GameContext::InGame,
            context_detail: b"Bed Wars".to_vec(),
            players: vec![p, PlayerFrame::default()],
            chat: vec![b"hello \"there\"".to_vec()],
            world_changed: true,
        };
        let line = frame_line(&f);
        assert_eq!(parse_frame(&line).unwrap(), f);
    }

    #[test]
    fn refusals() {
        assert_eq!(parse_frame(br#"{"pl":[{"e":1}]}"#).unwrap_err(), "malformed player");
        assert_eq!(parse_header(br#"{"kind":"vigil-trace","format":99}"#).unwrap_err(), "unsupported trace format 99");
        assert_eq!(read(b"").unwrap_err(), "empty trace");
    }
}
