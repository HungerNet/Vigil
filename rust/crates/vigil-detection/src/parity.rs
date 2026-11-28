//! The `VigilParity` format (tools/parity/parity.cpp), byte for byte: a digest of the engine's
//! entire state, the registry declaration and the corpus shapes. Two engines agree when these
//! texts are identical (P3A_PLAN.md §2).
//!
//! Every line goes through `vsnprintf` into a 4096-byte buffer in C++, so long lines (confidence
//! histories) are cut at 4095 bytes; [`Out::line`] does the same.

use vigil_formats::config::ParamType;

use crate::config::ParamView;
use crate::engine::Engine;
use crate::printf::{Arg, format_into};
use crate::types::WorldFrame;

/// VigilParity's FNV-1a (offset 1469598103934665603 — not the standard basis — prime 1099511628211).
pub fn fnv(s: &[u8]) -> u64 {
    let mut h: u64 = 1_469_598_103_934_665_603;
    for &c in s {
        h ^= u64::from(c);
        h = h.wrapping_mul(1_099_511_628_211);
    }
    h
}

/// `struct Out` of parity.cpp.
#[derive(Debug, Default)]
pub struct Out {
    pub s: Vec<u8>,
}

impl Out {
    pub fn line(&mut self, f: &str, args: &[Arg<'_>]) {
        self.s.extend(format_into(4096, f, args));
        self.s.push(b'\n');
    }
}

fn type_name(t: ParamType) -> &'static str {
    match t {
        ParamType::Bool => "bool",
        ParamType::Int => "int",
        ParamType::Float => "float",
        ParamType::Choice => "choice",
    }
}

/// `dumpRegistry`, for the registry `registerEngineSettings` builds from this engine's schema.
pub fn dump_registry(engine: &Engine, o: &mut Out) {
    let params = &engine.schema().params;
    let warn = engine.warn_predicates();
    o.line("registry %zu", &[Arg::U(params.len() as u64)]);
    for (i, p) in params.iter().enumerate() {
        // registerEngineSettings: Bool and Choice keep the default SettingRange {0, 1, 0, 2, ""}
        // (which is also what this schema holds for them); Int defaults go through int.
        let def = match p.ty {
            ParamType::Bool => {
                if p.default != 0.0 {
                    1.0
                } else {
                    0.0
                }
            }
            ParamType::Int | ParamType::Choice => f64::from(crate::cpp::to_int(p.default)),
            ParamType::Float => p.default,
        };
        o.line(
            "setting %s|%s|%s|%s|adv=%d|def=%.17g|value=%.17g|range=%.17g,%.17g,%.17g,%d,%s|deferred=%d",
            &[
                Arg::S(&p.key),
                Arg::S(type_name(p.ty).as_bytes()),
                Arg::S(&p.label),
                Arg::S(&p.group),
                Arg::I(i64::from(p.advanced)),
                Arg::F(def),
                Arg::F(def),
                Arg::F(p.min),
                Arg::F(p.max),
                Arg::F(p.step),
                Arg::I(i64::from(p.decimals)),
                Arg::S(&p.unit),
                Arg::I(0),
            ],
        );
        o.line("  tooltip %s", &[Arg::S(&p.tooltip)]);
        if !p.warning.is_empty() {
            o.line("  warning %s", &[Arg::S(&p.warning)]);
        }
        let mut choices = Vec::new();
        for c in &p.choices {
            choices.extend_from_slice(c);
            choices.push(b';');
        }
        if !choices.is_empty() {
            o.line("  choices %s", &[Arg::S(&choices)]);
        }
        let mut at = vec![def, 0.0, 1.0, -1.0, 2.0];
        let lo = if p.ty == ParamType::Bool { 0.0 } else { p.min };
        let hi = match p.ty {
            ParamType::Bool => 1.0,
            ParamType::Choice => p.choices.len() as f64,
            _ => p.max,
        };
        for k in 0..=40 {
            at.push(lo + (hi - lo) * f64::from(k) / 40.0);
        }
        let probes: Vec<u8> = at
            .iter()
            .map(|&v| {
                let active = !p.warning.is_empty() && warn[i].is_some_and(|w| w(ParamView { value: v }));
                if active { b'1' } else { b'0' }
            })
            .collect();
        o.line("  warnWhen %s", &[Arg::S(&probes)]);
    }
}

/// The `corpus <name> frames=… shape=…` line.
pub fn corpus_line(o: &mut Out, name: &[u8], frames: &[WorldFrame]) {
    let mut shape = Vec::new();
    for f in frames {
        shape.extend(f.tick.to_string().bytes());
        shape.push(b':');
        shape.extend(f.players.len().to_string().bytes());
        shape.push(b';');
    }
    o.line("corpus %s frames=%zu shape=%016llx", &[Arg::S(name), Arg::U(frames.len() as u64), Arg::U(fnv(&shape))]);
}

/// `summaryLine` of a generated run: what the scenario exercised.
pub fn summary_line(e: &Engine, name: &[u8]) -> Vec<u8> {
    let mut ind = Vec::new();
    let checks = e.checks();
    for (c, check) in checks.iter().enumerate() {
        let mut sum = vec![0i32; check.base.indicators.len()];
        for r in e.session().players() {
            for (i, &n) in r.checks[c].indicator_counts.iter().enumerate().take(sum.len()) {
                sum[i] += n;
            }
        }
        for (i, n) in sum.iter().enumerate() {
            ind.extend(n.to_string().bytes());
            if i + 1 < sum.len() {
                ind.push(b',');
            }
        }
        if c + 1 < checks.len() {
            ind.push(b';');
        }
    }
    let players = e.session().players();
    let suffixed = players.iter().filter(|r| !r.display_suffix.is_empty()).count();
    let exempt = players.iter().filter(|r| e.player_state(r).exempt).count();
    let st = e.stats();
    let mut o = Out::default();
    o.line(
        "summary %s records=%zu evidence=%d detections=%d alerts=%llu combat=%d stalls=%d filtered=%d pruned=%d ticks=%llu suffixed=%d exempt=%d scene=%d matches=%d ind=%s",
        &[
            Arg::S(name),
            Arg::U(players.len() as u64),
            Arg::from(e.session().total_evidence()),
            Arg::from(e.session().total_detections()),
            Arg::U(e.alert_seq()),
            Arg::from(st.combat_events),
            Arg::from(st.stalls),
            Arg::from(st.filtered_entities),
            Arg::from(st.pruned_records),
            Arg::U(st.ticks_processed),
            Arg::I(suffixed as i64),
            Arg::I(exempt as i64),
            Arg::from(e.scene()),
            Arg::I(i64::from(e.match_tracker().match_count() as u32 as i32)),
            Arg::S(&ind),
        ],
    );
    o.s
}

fn list<T>(items: impl IntoIterator<Item = T>, mut each: impl FnMut(T) -> Vec<u8>) -> Vec<u8> {
    let mut s = Vec::new();
    for x in items {
        s.extend(each(x));
    }
    s
}

/// `dumpEngine`: the engine's entire state.
pub fn dump_engine(e: &Engine, o: &mut Out) {
    o.line(
        "engine tick=%lld time=%.17g ctx=%d phase=%d scene=%d active=%d recording=%d stalled=%d monitoring=%d",
        &[
            Arg::I(e.last_tick()),
            Arg::F(e.last_time_ms()),
            Arg::I(e.context() as i64),
            Arg::I(e.phase() as i64),
            Arg::I(i64::from(e.scene())),
            Arg::from(e.active()),
            Arg::from(e.recording()),
            Arg::from(e.stalled()),
            Arg::from(e.monitoring_enabled()),
        ],
    );
    let st = e.stats();
    o.line(
        "stats tracked=%d filtered=%d ticks=%llu skipped=%d combat=%d stalls=%d pruned=%d",
        &[
            Arg::from(st.tracked_players),
            Arg::from(st.filtered_entities),
            Arg::U(st.ticks_processed),
            Arg::from(st.skipped_ticks),
            Arg::from(st.combat_events),
            Arg::from(st.stalls),
            Arg::from(st.pruned_records),
        ],
    );
    // matchCount() is a uint64_t passed to %d: printf reads its low 32 bits.
    o.line("match count=%d", &[Arg::I(i64::from(e.match_tracker().match_count() as u32 as i32))]);
    o.line(
        "session evidence=%d detections=%d revision=%llu",
        &[
            Arg::from(e.session().total_evidence()),
            Arg::from(e.session().total_detections()),
            Arg::U(e.session().revision()),
        ],
    );
    for f in e.filtered() {
        o.line(
            "filtered %d %s kind=%d %s",
            &[Arg::from(f.entity_id), Arg::S(&f.name), Arg::I(f.kind as i64), Arg::S(&f.reason)],
        );
    }
    for c in e.recent_combat() {
        o.line(
            "combat id=%llu tick=%lld a=%d v=%d cand=%d cert=%.9g reach=%.17g raw=%.17g aim=%.17g yawErr=%.17g",
            &[
                Arg::U(c.id),
                Arg::I(c.tick),
                Arg::from(c.attacker),
                Arg::from(c.victim),
                Arg::from(c.candidates),
                Arg::from(c.certainty),
                Arg::F(c.reach),
                Arg::F(c.reach_raw),
                Arg::F(c.aim_outside),
                Arg::F(c.yaw_error),
            ],
        );
    }
    o.line("alerts seq=%llu", &[Arg::U(e.alert_seq())]);
    for a in e.alerts() {
        o.line(
            "alert %llu t=%.17g %s %s check=%d conf=%.9g %s",
            &[
                Arg::U(a.seq),
                Arg::F(a.time_ms),
                Arg::S(&a.player_key),
                Arg::S(&a.name),
                Arg::from(a.check),
                Arg::from(a.confidence),
                Arg::S(&a.summary),
            ],
        );
        for r in &a.reasons {
            o.line("  reason %s", &[Arg::S(r)]);
        }
    }
    for d in e.debug_log() {
        o.line(
            "debug t=%.17g tick=%lld check=%d %s %s",
            &[Arg::F(d.time_ms), Arg::I(d.tick), Arg::from(d.check), Arg::S(&d.player), Arg::S(&d.text)],
        );
    }
    for r in e.session().players() {
        let ps = e.player_state(r);
        o.line(
            "player %s|%s|%s%s ent=%d present=%d local=%d mate=%d first=%.17g last=%.17g susp=%.9g peak=%.9g scene=%d match=%d seen=%d part=%d top=%d",
            &[
                Arg::S(&r.key),
                Arg::S(&r.uuid),
                Arg::S(&r.name),
                Arg::S(&r.display_suffix),
                Arg::from(r.entity_id),
                Arg::from(r.present),
                Arg::from(r.local),
                Arg::from(r.teammate),
                Arg::F(r.first_seen_ms),
                Arg::F(r.last_seen_ms),
                Arg::from(r.suspicion),
                Arg::from(r.peak_suspicion),
                Arg::from(r.scene),
                Arg::from(r.match_scene),
                Arg::from(r.scenes_seen),
                Arg::from(r.participant),
                Arg::from(r.top_check()),
            ],
        );
        o.line(
            "  state listed=%d local=%d mate=%d exempt=%d game=%d judged=%d",
            &[
                Arg::from(ps.listed),
                Arg::from(ps.local),
                Arg::from(ps.teammate),
                Arg::from(ps.exempt),
                Arg::from(ps.in_this_game),
                Arg::from(ps.judged),
            ],
        );
        let b = &r.stats;
        o.line(
            "  stats obs=%d combat=%d landed=%d taken=%d swings=%d reach=%.17g/%d max=%.17g aim=%.17g cps=%.17g/%.17g speed=%.17g/%.17g/%d jumps=%d air=%.17g",
            &[
                Arg::from(b.ticks_observed),
                Arg::from(b.ticks_in_combat),
                Arg::from(b.hits_landed),
                Arg::from(b.hits_taken),
                Arg::from(b.swings),
                Arg::F(b.reach_sum),
                Arg::from(b.reach_samples),
                Arg::F(b.reach_max),
                Arg::F(b.aim_outside_sum),
                Arg::F(b.cps_peak),
                Arg::F(b.cps_recent),
                Arg::F(b.speed_peak),
                Arg::F(b.speed_sum),
                Arg::from(b.speed_samples),
                Arg::from(b.jumps),
                Arg::F(b.air_ticks_sum),
            ],
        );
        let sr = list(&b.speed_recent, |&v| format_into(64, "%.9g,", &[Arg::from(v)]));
        let rr = list(&b.reach_recent, |&v| format_into(64, "%.9g,", &[Arg::from(v)]));
        let sh =
            list(&r.suspicion_history, |p| format_into(64, "%.17g:%.9g,", &[Arg::F(p.time_ms), Arg::from(p.value)]));
        o.line("  speedRecent %s", &[Arg::S(&sr)]);
        o.line("  reachRecent %s", &[Arg::S(&rr)]);
        o.line("  suspicionHistory %s", &[Arg::S(&sh)]);
        for (i, cr) in r.checks.iter().enumerate() {
            let ic = list(&cr.indicator_counts, |n| {
                let mut v = n.to_string().into_bytes();
                v.push(b',');
                v
            });
            let hist = list(&cr.history, |p| format_into(64, "%.17g:%.9g,", &[Arg::F(p.time_ms), Arg::from(p.value)]));
            o.line(
                "  check %zu value=%.9g lastDet=%.17g peak=%.9g ev=%d det=%d lastEv=%.17g sampled=%.9g ind=%s",
                &[
                    Arg::U(i as u64),
                    Arg::from(cr.model.value()),
                    Arg::F(cr.model.last_detection_ms()),
                    Arg::from(cr.peak),
                    Arg::from(cr.evidence),
                    Arg::from(cr.detections),
                    Arg::F(cr.last_evidence_ms),
                    Arg::from(cr.last_sampled),
                    Arg::S(&ic),
                ],
            );
            o.line("    history %s", &[Arg::S(&hist)]);
        }
        for ev in &r.evidence {
            o.line(
                "  evidence %llu tick=%lld t=%.17g check=%d ind=%d w=%.9g sev=%.9g +%.9g ->%.9g",
                &[
                    Arg::U(ev.id),
                    Arg::I(ev.tick),
                    Arg::F(ev.time_ms),
                    Arg::from(ev.check),
                    Arg::from(ev.indicator),
                    Arg::from(ev.weight),
                    Arg::from(ev.severity),
                    Arg::from(ev.contribution),
                    Arg::from(ev.confidence_after),
                ],
            );
            o.line("    summary %s", &[Arg::S(&ev.summary)]);
            o.line("    detail %s", &[Arg::S(&ev.detail)]);
            o.line("    target %s", &[Arg::S(&ev.target)]);
            for v in &ev.values {
                o.line(
                    "    value %s=%.17g %s limit=%.17g has=%d role=%d",
                    &[
                        Arg::S(&v.name),
                        Arg::F(v.value),
                        Arg::S(&v.unit),
                        Arg::F(v.limit),
                        Arg::from(v.has_limit),
                        Arg::I(v.role as i64),
                    ],
                );
            }
            let en = &ev.envelope;
            o.line(
                "    envelope %s %s obs=%.17g limit=%.17g exp=%.17g has=%d %s upper=%d",
                &[
                    Arg::S(&en.quantity),
                    Arg::S(&en.unit),
                    Arg::F(en.observed),
                    Arg::F(en.limit),
                    Arg::F(en.expected),
                    Arg::from(en.has_expected),
                    Arg::S(&en.expected_label),
                    Arg::from(en.upper),
                ],
            );
            for c in &en.context {
                o.line("      context %s", &[Arg::S(c)]);
            }
        }
        for d in &r.detections {
            let ids = list(&d.evidence, |id| {
                let mut v = id.to_string().into_bytes();
                v.push(b',');
                v
            });
            o.line(
                "  detection %llu tick=%lld t=%.17g check=%d conf=%.9g n=%d distinct=%d ev=%s %s",
                &[
                    Arg::U(d.id),
                    Arg::I(d.tick),
                    Arg::F(d.time_ms),
                    Arg::from(d.check),
                    Arg::from(d.confidence),
                    Arg::from(d.evidence_count),
                    Arg::from(d.distinct_indicators),
                    Arg::S(&ids),
                    Arg::S(&d.summary),
                ],
            );
        }
        for t in &r.timeline {
            o.line(
                "  timeline %d t=%.17g tick=%lld check=%d ref=%llu v=%.9g %s",
                &[
                    Arg::I(t.kind as i64),
                    Arg::F(t.time_ms),
                    Arg::I(t.tick),
                    Arg::from(t.check),
                    Arg::U(t.reference),
                    Arg::from(t.value),
                    Arg::S(&t.text),
                ],
            );
        }
    }
}
