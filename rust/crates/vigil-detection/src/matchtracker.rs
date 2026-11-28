//! `src/detection/match.{h,cpp}`: game phases and scenes.

use crate::types::GameContext;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
#[repr(u8)]
pub enum GamePhase {
    #[default]
    Idle = 0,
    Lobby = 1,
    Queue = 2,
    Match = 3,
    PostGame = 4,
    Replay = 5,
}

pub fn game_phase_name(p: GamePhase) -> &'static str {
    match p {
        GamePhase::Lobby => "Lobby",
        GamePhase::Queue => "Queue",
        GamePhase::Match => "Match",
        GamePhase::PostGame => "Post-game",
        GamePhase::Replay => "Replay",
        GamePhase::Idle => "Idle",
    }
}

/// End-of-game announcements; player chat (`Name: winner: me`) never matches.
pub fn matches_game_end(clean_line: &[u8]) -> bool {
    const TRIGGERS: [&[u8]; 8] = [
        b"1st killer",
        b"reward summary",
        b"winner:",
        b"winner -",
        b"winners:",
        b"winners -",
        b"victory!",
        b"game over!",
    ];
    if clean_line.is_empty() {
        return false;
    }
    let l: Vec<u8> = clean_line.iter().map(u8::to_ascii_lowercase).collect();
    let colon = l.iter().position(|&c| c == b':');
    for t in TRIGGERS {
        let Some(pos) = find(&l, t) else { continue };
        if colon.is_some_and(|c| c < pos) {
            continue;
        }
        return true;
    }
    false
}

/// `std::string::find`.
fn find(hay: &[u8], needle: &[u8]) -> Option<usize> {
    if needle.is_empty() {
        return Some(0);
    }
    hay.windows(needle.len()).position(|w| w == needle)
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Update {
    pub scene_changed: bool,
    pub match_started: bool,
    pub match_ended: bool,
}

#[derive(Clone, Debug)]
pub struct MatchTracker {
    phase: GamePhase,
    last_solid: GamePhase,
    post_game: bool,
    end_reported: bool,
    pending_scene_change: bool,
    scene: i32,
    match_scene: i32,
    queue_scene: i32,
    match_started_ms: f64,
    match_ended_ms: f64,
    matches: u64,
}

impl Default for MatchTracker {
    fn default() -> Self {
        MatchTracker {
            phase: GamePhase::Idle,
            last_solid: GamePhase::Idle,
            post_game: false,
            end_reported: true,
            pending_scene_change: false,
            scene: 0,
            match_scene: -1,
            queue_scene: -1,
            match_started_ms: -1.0,
            match_ended_ms: -1.0,
            matches: 0,
        }
    }
}

impl MatchTracker {
    pub fn phase(&self) -> GamePhase {
        self.phase
    }
    pub fn scene(&self) -> i32 {
        self.scene
    }
    pub fn match_scene(&self) -> i32 {
        self.match_scene
    }
    pub fn queue_scene(&self) -> i32 {
        self.queue_scene
    }
    pub fn match_started_ms(&self) -> f64 {
        self.match_started_ms
    }
    pub fn match_ended_ms(&self) -> f64 {
        self.match_ended_ms
    }
    pub fn match_count(&self) -> u64 {
        self.matches
    }
    pub fn in_match(&self) -> bool {
        self.phase == GamePhase::Match || self.phase == GamePhase::PostGame
    }

    pub fn on_world_changed(&mut self) {
        self.pending_scene_change = true;
    }

    fn report_end(&mut self, u: &mut Update, now_ms: f64) {
        if self.end_reported {
            return;
        }
        self.end_reported = true;
        u.match_ended = true;
        self.match_ended_ms = now_ms;
    }

    pub fn on_frame(&mut self, context: GameContext, chat: &[Vec<u8>], now_ms: f64) -> Update {
        let mut u = Update::default();
        if self.pending_scene_change {
            self.pending_scene_change = false;
            self.scene += 1;
            u.scene_changed = true;
            self.report_end(&mut u, now_ms);
            self.post_game = false;
            self.last_solid = GamePhase::Idle;
        }

        let base = match context {
            GameContext::Lobby => GamePhase::Lobby,
            GameContext::Pregame => GamePhase::Queue,
            GameContext::InGame => GamePhase::Match,
            GameContext::Replay => GamePhase::Replay,
            GameContext::Unknown => GamePhase::Idle,
        };
        let mut next = if base == GamePhase::Idle { self.last_solid } else { base };
        if next == GamePhase::PostGame {
            next = GamePhase::Match;
        }

        let in_match_before = self.last_solid == GamePhase::Match || self.last_solid == GamePhase::PostGame;
        let in_match_now = next == GamePhase::Match;
        if in_match_now && !in_match_before {
            self.queue_scene = if !u.scene_changed && self.last_solid == GamePhase::Queue { self.scene } else { -1 };
            if !u.scene_changed {
                self.scene += 1;
                u.scene_changed = true;
            }
            u.match_started = true;
            self.match_scene = self.scene;
            self.match_started_ms = now_ms;
            self.end_reported = false;
            self.post_game = false;
            self.matches += 1;
        }
        if in_match_now {
            for line in chat {
                if !self.post_game && matches_game_end(line) {
                    self.post_game = true;
                    self.report_end(&mut u, now_ms);
                }
            }
        } else {
            if in_match_before {
                self.report_end(&mut u, now_ms);
            }
            self.post_game = false;
        }

        if in_match_now && self.post_game {
            next = GamePhase::PostGame;
        }
        if next != GamePhase::Idle {
            self.last_solid = next;
        }
        self.phase = next;
        u
    }
}
