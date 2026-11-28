//! `checks/autoblock.cpp` — attacking while continuously blocking.

use super::prelude::*;

#[derive(Debug, Default)]
struct AutoBlock {
    i_swing: i32,
    min_block: ParamId,
}

pub fn make() -> Check {
    let mut b = CheckBase::new(
        "autoblock",
        "AutoBlock",
        CheckCategory::Combat,
        "Attacking while holding a sword block",
        "In vanilla 1.8 a player cannot swing while their sword is raised: clicks are consumed until the block is released. Vigil counts new swings that begin while the player has been blocking continuously for several ticks and is still blocking afterwards.",
    );
    b.def_alert = 70.0;
    b.def_half_life = 40.0;
    b.def_min_evidence = 4;
    Check::new(b, Box::new(AutoBlock::default()))
}

impl CheckImpl for AutoBlock {
    fn register(&mut self, r: &mut Registrar<'_>) {
        self.i_swing =
            r.indicator("swing_block", "Swing while blocking", "A swing began during a continuous block.", 1.0);
        self.min_block = r.int(
            "minBlockTicks",
            "Block duration",
            3,
            range(1.0, 10.0, 1.0, 0, "ticks"),
            "How long the block must have been held before a swing counts. Fast legitimate block-hitting releases and re-raises the sword between swings.",
        );
        r.preset(self.min_block, 4.0, 3.0, 2.0);
    }

    fn on_tick(&self, ctx: &mut Ctx<'_>, t: &PlayerTrack, _s: &mut dyn Any) {
        let cfg = ctx.config();
        if !t.swing_started() || !t.has(1) {
            return;
        }
        let f = t.cur();
        let p = t.back(1);
        if !f.blocking || !p.blocking || !mc::is_sword(f.held_item) {
            return;
        }
        let held = ctx.tick() - t.last_block_start_tick();
        if held < Tick::from(cfg.i(self.min_block)) {
            return;
        }
        ctx.emit(
            t,
            self.i_swing,
            cpp::clamp(held as f32 / 10.0, 0.5, 1.0),
            fmt!("Swung after blocking for %lld ticks", held),
            fmt!(
                "Swung with the sword still raised after %lld ms. In vanilla the block has to be released before an attack.",
                held.wrapping_mul(50)
            ),
            vec![V::limited("Block held", held as f64, "ticks", f64::from(cfg.i(self.min_block)))],
            none(),
        );
    }
}
