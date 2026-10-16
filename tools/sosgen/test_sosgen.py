# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Unit tests for sosgen: every build-time check has a counter-example."""

import copy
import io
import sys
import tempfile
import tomllib
import unittest
from contextlib import redirect_stderr
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import sosgen  # noqa: E402
from sosgen import ConfigError, Platform  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
PIPELINE = ROOT / "examples" / "layered_pipeline" / "app.toml"

BASE = {
    "task": [
        {"name": "lo", "prio": 5, "trigger": ["periodic"], "period": "10ms", "run": "lo_run"},
        {"name": "hi", "prio": 1, "trigger": ["event"], "events": ["A", "B"], "run": "hi_run"},
        {"name": "bg", "trigger": ["background"], "run": "bg_run"},
    ],
    "ring": [
        {"name": "q", "elem_type": "uint32_t", "capacity": 8,
         "producer": "async", "consumer": "hi", "notify": "B"},
    ],
}


def cfg_with(mutate):
    doc = copy.deepcopy(BASE)
    mutate(doc)
    return doc


class Valid(unittest.TestCase):
    def test_sorting_and_ids(self):
        cfg = sosgen.parse(copy.deepcopy(BASE))
        self.assertEqual([t.name for t in cfg.tasks], ["hi", "lo", "bg"])
        self.assertEqual([t.tid for t in cfg.tasks], [0, 1, 2])
        hi = cfg.tasks[0]
        self.assertEqual(hi.bit("A"), 1)
        self.assertEqual(hi.bit("B"), 2)

    def test_time_units(self):
        cfg = sosgen.parse(cfg_with(lambda d: d.update(kernel={"time_unit_us": 10})))
        self.assertEqual(cfg.tasks[1].period, 1000)            # 10 ms in 10 us units

    def test_output_contents(self):
        cfg = sosgen.parse(copy.deepcopy(BASE))
        h = sosgen.render_gen_h(cfg)
        c = sosgen.render_gen_c(cfg)
        k = sosgen.render_cfg_h(cfg)
        self.assertIn("#define SOS_TASK_HI", h)
        self.assertIn("#define SOS_EV_HI_B", h)
        self.assertIn("((sos_events_t)0x00000002u)", h)
        self.assertIn("#define SOS_RING_Q", h)
        self.assertIn("not covered by\n * the MPL-2.0", h)
        self.assertIn("static uint32_t l_ring_q[8];", c)
        self.assertIn(".notify = 0x00000002u", c)
        self.assertIn(".producer = SOS_TASK_NONE,", c)       # async producer
        self.assertIn(".consumer = SOS_TASK_HI", c)
        self.assertIn("#define SOS_CFG_RING_ENABLE         1", k)
        self.assertIn("#define SOS_CFG_RING_ZERO_COPY      1", k)
        self.assertIn("#define SOS_CFG_SUPERVISE_ENABLE    1", k)
        self.assertIn(f".abi_version = {sosgen.ABI_VERSION}u", c)
        self.assertIn(f".digest = 0x{cfg.digest:08X}u", c)                  # literal (D14)
        self.assertIn("#define SOS_CFG_INIT() sos_init_checked(&sos_cfg_root, SOS_CFG_DIGEST)", h)
        self.assertIn("#define SOS_CFG_MAX_TASKS           3u", k)

    def test_no_rings(self):
        cfg = sosgen.parse(cfg_with(lambda d: d.pop("ring")))
        c = sosgen.render_gen_c(cfg)
        k = sosgen.render_cfg_h(cfg)
        self.assertIn(".rings = NULL,", c)
        self.assertIn("#define SOS_CFG_MAX_RINGS           1u", k)
        self.assertIn("#define SOS_CFG_RING_ENABLE         0", k)   # not compiled in
        self.assertIn("#define SOS_CFG_RING_ZERO_COPY      0", k)

    def test_task_producer_and_zero_copy_off(self):
        def m(d):
            d["ring"][0]["producer"] = "lo"
            d["kernel"] = {"ring_zero_copy": False, "supervise_enable": False,
                           "health_enable": False}
        cfg = sosgen.parse(cfg_with(m))
        self.assertIn(".producer = SOS_TASK_LO,", sosgen.render_gen_c(cfg))
        k = sosgen.render_cfg_h(cfg)
        self.assertIn("#define SOS_CFG_RING_ZERO_COPY      0", k)
        self.assertIn("#define SOS_CFG_SUPERVISE_ENABLE    0", k)

    def test_deterministic(self):
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            ra = sosgen.generate(PIPELINE, a)
            rb = sosgen.generate(PIPELINE, b)
            self.assertEqual(ra, rb)
            for name in ("sos_cfg.h", "sos_cfg_gen.h", "sos_cfg_gen.c"):
                self.assertEqual((Path(a) / name).read_bytes(), (Path(b) / name).read_bytes())

    def test_digest_tracks_content(self):
        d1 = sosgen.parse(copy.deepcopy(BASE)).digest
        d2 = sosgen.parse(cfg_with(lambda d: d["task"][0].update(period="11ms"))).digest
        self.assertNotEqual(d1, d2)

    def test_pipeline_example(self):
        with open(PIPELINE, "rb") as f:
            cfg = sosgen.parse(tomllib.load(f), "app.toml")
        verdicts = dict(sosgen.schedulability(cfg))
        # ingress: B = 200 (worker step), n = 1; D = min(1 ms, 400 us)
        self.assertEqual(verdicts["ingress"], "PASS: R = 300 <= D = 400")
        # worker: k = 2, n = 3: 2*60 + 3*200 + (ceil(R/1000) + ceil(R/400)) * 100
        self.assertEqual(verdicts["worker"], "PASS: R = 1320 <= D = 3600")
        # heartbeat: 60 + 20 + ingress releases * 100 + ceil(R/1200) * 3 * 200
        self.assertEqual(verdicts["heartbeat"], "PASS: R = 1180 <= D = 5000")


class Rejects(unittest.TestCase):
    def bad(self, mutate, needle):
        with self.assertRaises(ConfigError) as cm:
            sosgen.parse(cfg_with(mutate))
        self.assertIn(needle, str(cm.exception))

    # check 1
    def test_duplicate_prio(self):
        self.bad(lambda d: d["task"][1].update(prio=5), "already used")

    def test_too_many_foreground(self):
        def m(d):
            d["task"] = [{"name": f"t{i}", "prio": i, "trigger": ["event"], "events": ["E"],
                          "run": "r"} for i in range(33)]
            d.pop("ring")
        self.bad(m, "at most 32")

    # check 2
    def test_background_combined(self):
        self.bad(lambda d: d["task"][2].update(trigger=["background", "event"]),
                 "cannot be combined")

    def test_background_with_prio(self):
        self.bad(lambda d: d["task"][2].update(prio=3), "not allowed for a background")

    def test_unknown_trigger(self):
        self.bad(lambda d: d["task"][0].update(trigger=["sometimes"]), "unknown trigger")

    # check 3
    def test_periodic_without_period(self):
        self.bad(lambda d: d["task"][0].pop("period"), "needs 'period'")

    def test_period_without_periodic(self):
        self.bad(lambda d: d["task"][1].update(period="1ms"), "not periodic")

    def test_event_without_events(self):
        def m(d):
            d["task"][1]["events"] = []
            d.pop("ring")
        self.bad(m, "at least one entry")

    def test_too_many_events(self):
        self.bad(lambda d: d["task"][1].update(events=[f"E{i}" for i in range(32)]),
                 "at most 31")

    def test_duplicate_event(self):
        self.bad(lambda d: d["task"][1].update(events=["A", "A"]), "duplicate event")

    # check 4
    def test_capacity_not_power_of_two(self):
        self.bad(lambda d: d["ring"][0].update(capacity=6), "power of two")

    def test_unknown_producer(self):
        self.bad(lambda d: d["ring"][0].update(producer="nobody"), "'producer'")

    def test_unknown_consumer(self):
        self.bad(lambda d: d["ring"][0].update(consumer="nobody"), "'consumer'")

    def test_notify_not_declared(self):
        self.bad(lambda d: d["ring"][0].update(notify="C"), "not an event declared")

    # check 5
    def test_producer_is_consumer(self):
        self.bad(lambda d: d["ring"][0].update(producer="hi"), "must differ")

    # check 6
    def test_time_not_whole_units(self):
        def m(d):
            d["kernel"] = {"time_unit_us": 10}
            d["task"][0]["period"] = "15us"
        self.bad(m, "whole number")

    def test_time_without_unit(self):
        self.bad(lambda d: d["task"][0].update(period=10), "string with a unit")

    def test_time_unit_must_divide_second(self):
        self.bad(lambda d: d.update(kernel={"time_unit_us": 7}), "divide")

    # optional modules (D12)
    def test_rings_declared_but_disabled(self):
        self.bad(lambda d: d.update(kernel={"ring_enable": False}), "'ring_enable' is false")

    def test_zero_copy_without_rings(self):
        self.bad(lambda d: (d.pop("ring"), d.update(kernel={"ring_zero_copy": True})),
                 "requires 'ring_enable'")

    def test_alive_window_without_supervision(self):
        def m(d):
            d["kernel"] = {"supervise_enable": False}
            d["task"][0]["alive_window"] = "30ms"
        self.bad(m, "requires supervise_enable")

    # general
    def test_unknown_key(self):
        self.bad(lambda d: d["task"][0].update(perod="1ms"), "unknown key(s): perod")

    def test_bad_identifier(self):
        self.bad(lambda d: d["task"][0].update(run="lo-run"), "C identifier")

    def test_duplicate_task_name(self):
        self.bad(lambda d: d["task"][2].update(name="LO"), "duplicate task name")

    def test_no_tasks(self):
        self.bad(lambda d: (d.pop("task"), d.pop("ring")), "at least one task")


class PlatformChecks(unittest.TestCase):
    # check 7
    def test_range_16bit(self):
        cfg = sosgen.parse(cfg_with(lambda d: d["task"][0].update(period="20ms")))
        with self.assertRaises(ConfigError) as cm:
            sosgen.check_platform(cfg, Platform(16, 1_000_000))
        self.assertIn("allows < 16384", str(cm.exception))
        sosgen.check_platform(cfg, Platform(32, 1_000_000))     # fine with 32 bits

    # check 8
    def test_precision_warning(self):
        cfg = sosgen.parse(cfg_with(lambda d: d["task"][0].update(budget="5us")))
        sosgen.check_platform(cfg, Platform(32, 1_000_000))
        self.assertEqual(len(cfg.warnings), 1)
        self.assertIn("budget/10", cfg.warnings[0])

    # check 9
    def test_schedulability_miss(self):
        def m(d):
            d["task"][0].update(budget="9ms")                  # lo: C 9 ms, T 10 ms
            d["task"][1].update(budget="2ms", min_interarrival="3ms")
            d["task"][2].update(budget="1ms")
        cfg = sosgen.parse(cfg_with(m))
        verdicts = dict(sosgen.schedulability(cfg))
        self.assertTrue(verdicts["lo"].startswith("FAIL"))
        # non-preemptive: hi may wait for one whole 9 ms step of lo -> 9 + 2 > 3
        self.assertTrue(verdicts["hi"].startswith("FAIL: R = 11000 > D = 3000"))

    def test_schedulability_uses_shorter_interval(self):
        # periodic 10 ms but events as often as every 1 ms: interference uses 1 ms
        def m(d):
            d["task"][1].update(budget="100us", min_interarrival="1ms")
            d["task"][0].update(trigger=["periodic", "event"], events=["X"], budget="300us",
                                min_interarrival="1ms", max_steps=1)
            d["task"][2].update(budget="50us")
        cfg = sosgen.parse(cfg_with(m))
        lo = dict(sosgen.schedulability(cfg))["lo"]
        # 50 (bg) + 300 + hi releases: ceil(R/1ms)*100; lo's D = min(10 ms, 1 ms)
        self.assertTrue(lo.startswith("CONDITIONAL: R = 450 <= D = 1000"), lo)

    def test_schedulability_counts_steps_and_yields(self):
        def m(d):
            d["task"][1].update(budget="100us", min_interarrival="10ms", max_steps=20,
                                again_limit=5)
            d["task"][0].update(budget="200us", max_steps=1)
            d["task"][2].update(budget="50us")
        cfg = sosgen.parse(cfg_with(m))
        v = dict(sosgen.schedulability(cfg))
        # hi: n = 20, k = 1 + 19 // 5 = 4 -> 4*200 + 20*100 = 2800
        self.assertEqual(v["hi"], "PASS: R = 2800 <= D = 10000")
        # lo sees hi's whole job (20 steps) as interference: 50 + 200 + 2000
        self.assertEqual(v["lo"], "PASS: R = 2250 <= D = 10000")

    def test_schedulability_two_release_streams(self):
        # H: periodic 10 ms AND events every >= 10 ms -> up to 2 releases in a short
        # window (event just before and just after the periodic release)
        doc = {"task": [
            {"name": "h", "prio": 0, "trigger": ["periodic", "event"], "period": "10ms",
             "events": ["E"], "min_interarrival": "10ms", "budget": "1ms", "max_steps": 1,
             "run": "h_run"},
            {"name": "l", "prio": 1, "trigger": ["periodic"], "period": "20ms",
             "budget": "1ms", "max_steps": 1, "run": "l_run"}]}
        v = dict(sosgen.schedulability(sosgen.parse(doc)))
        self.assertEqual(v["l"], "PASS: R = 3000 <= D = 20000")   # min() model gave 2000

    def test_schedulability_event_rate_required(self):
        doc = {"task": [{"name": "e", "prio": 0, "trigger": ["periodic", "event"],
                         "period": "10ms", "events": ["E"], "budget": "1ms", "run": "r"}]}
        v = dict(sosgen.schedulability(sosgen.parse(doc)))
        self.assertTrue(v["e"].startswith("N/A (an event-triggered task needs min_interarrival"))

    def test_dispatch_overhead(self):
        doc = {"kernel": {"dispatch_overhead": "10us"}, "task": [
            {"name": "a", "prio": 0, "trigger": ["periodic"], "period": "1ms",
             "budget": "100us", "max_steps": 2, "run": "a_run"},
            {"name": "b", "trigger": ["background"], "budget": "40us", "run": "b_run"}]}
        v = dict(sosgen.schedulability(sosgen.parse(doc)))
        # B = 40 + 10, C = 100 + 10, n = 2 -> 50 + 220
        self.assertEqual(v["a"], "PASS: R = 270 <= D = 1000")

    def test_schedulability_marks_assumptions(self):
        def m(d):
            d["task"][1].update(budget="100us", min_interarrival="1ms")
            d["task"][0].update(budget="200us")
            d["task"][2].update(budget="50us")
        v = dict(sosgen.schedulability(sosgen.parse(cfg_with(m))))
        self.assertIn("assumes 1 step per job for: lo, hi", v["lo"])

    def test_schedulability_needs_budgets(self):
        cfg = sosgen.parse(copy.deepcopy(BASE))                 # no budgets at all
        self.assertTrue(all(v.startswith("N/A") for _, v in sosgen.schedulability(cfg)))


class Cli(unittest.TestCase):
    def test_error_exit_code(self):
        with tempfile.TemporaryDirectory() as tmp:
            bad = Path(tmp) / "bad.toml"
            bad.write_text('[[task]]\nname = "x"\nprio = 0\ntrigger = ["event"]\nrun = "r"\n')
            err = io.StringIO()
            with redirect_stderr(err):
                rc = sosgen.main([str(bad), "-o", tmp])
            self.assertEqual(rc, 1)
            self.assertIn("bad.toml: task[0] 'x'", err.getvalue())


if __name__ == "__main__":
    unittest.main()
