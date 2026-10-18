# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Independent check of the schedulability estimate (sosgen check 9).

A step-level model of sos_poll() -- ready bits, D13 yields, background idle
slot, merging of releases -- runs random task sets with random release
patterns, step lengths, step counts and alarms. Every response time it
observes must stay within the bound R that sosgen reports as PASS or
CONDITIONAL. The model is written from the kernel source, not from the
formula, so the two can disagree; a disagreement means the formula is
optimistic. A simulation finds lower bounds only: passing does not prove
the formula, failing disproves it."""

import random
import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sosgen  # noqa: E402


class _St:
    """Per foreground task: next release of each stream, pending and
    in-progress releases, D13 counters."""

    def __init__(self):
        self.per = None          # next periodic release time
        self.ev = None           # next event arrival time
        self.alarm = None        # pending alarm expiry
        self.pend = []           # releases waiting for the next job (ready bit set)
        self.job = None          # releases served by the job in progress
        self.left = 0            # steps left in the job in progress
        self.again = 0           # again_cnt


def simulate(cfg, horizon, rng):
    """Run the model; return the worst observed response time per task name."""
    ov = cfg.dispatch_overhead
    fg = [t for t in cfg.tasks if not t.background]       # index = bit = priority order
    bg = [t for t in cfg.tasks if t.background]
    st = [_St() for _ in fg]
    worst = {t.name: 0 for t in fg}

    def gap(T):                  # sporadic: at least T apart, often exactly T
        return T if rng.random() < 0.7 else T + rng.randrange(T)

    for t, s in zip(fg, st):
        if "periodic" in t.triggers:
            s.per = t.offset + rng.randrange(t.period)
        if "event" in t.triggers:
            s.ev = rng.randrange(2 * t.min_interarrival)

    def arrivals(i, upto):
        t, s, out = fg[i], st[i], []
        while s.per is not None and s.per <= upto:
            out.append((s.per, "p"))
            s.per += t.period
        while s.ev is not None and s.ev <= upto:
            out.append((s.ev, "e"))
            s.ev += gap(t.min_interarrival)
        if s.alarm is not None and s.alarm <= upto:
            out.append((s.alarm, "a"))
            s.alarm = None
        return out

    def next_arrival():
        times = [x for s in st for x in (s.per, s.ev, s.alarm) if x is not None]
        return min(times)

    now, last, deferred = 0, None, set()
    while now < horizon:
        # release scan at the start of sos_poll (events are in fact seen at once;
        # what matters is where each release lands relative to the steps)
        for i in range(len(fg)):
            s = st[i]
            for r, kind in sorted(arrivals(i, now)):
                if last is not None and last[0] == i and last[2] and r > last[1]:
                    # during the final (OK) step of i, which ended now
                    if kind == "p":
                        # OK advances next_release past r: the release is served
                        worst[fg[i].name] = max(worst[fg[i].name], now - r)
                    else:
                        s.pend.append(r)          # ready bit set after dispatch: new job
                elif s.job is not None:
                    s.job.append(r)               # merges into the job in progress
                else:
                    s.pend.append(r)              # merges into the pending job
        ready = [i for i, s in enumerate(st) if s.job is not None or s.pend]
        cand = [i for i in ready if i not in deferred]
        i = None
        if cand:
            i = cand[0]
        elif bg:
            b = bg[rng.randrange(len(bg))]
            now += (b.budget if rng.random() < 0.6 else rng.randint(1, b.budget)) + ov
            deferred.clear()                      # a background step serves all yields
            last = None
            continue
        elif deferred:
            deferred.clear()
            if ready:
                i = ready[0]
        if i is None:
            now = max(now + 1, next_arrival())
            last = None
            continue

        t, s = fg[i], st[i]
        if s.job is None:
            s.job, s.pend = s.pend, []
            n = t.max_steps or 1
            s.left = n if rng.random() < 0.5 else rng.randint(1, n)
        start = now
        dur = (t.budget if rng.random() < 0.6 else rng.randint(1, t.budget)) + ov
        now += dur
        d = getattr(t, "min_alarm_delay", 0)
        if d and rng.random() < 0.4:              # sos_task_set_alarm() during the step
            s.alarm = start + rng.randint(0, dur) + gap(d)
        deferred -= {j for j in deferred if j <= i}   # l_yield_consumed
        s.left -= 1
        if s.left:                                # SOS_RUN_AGAIN
            s.again += 1
            if t.again_limit and s.again >= t.again_limit:
                deferred.add(i)
                s.again = 0
        else:                                     # SOS_RUN_OK
            s.again = 0
            for r in s.job:
                worst[t.name] = max(worst[t.name], now - r)
            s.job = None
        last = (i, start, s.left == 0)
    return worst


def random_doc(rng):
    tasks = []
    for p in range(rng.randint(2, 4)):
        trig = rng.choice([["periodic"], ["event"], ["periodic", "event"]])
        n = rng.randint(1, 3)
        t = {"name": f"t{p}", "prio": p, "trigger": trig, "run": "r",
             "budget": f"{rng.randint(20, 400)}us", "max_steps": n}
        if "periodic" in trig:
            t["period"] = f"{rng.randint(2, 20) * 500}us"
        if "event" in trig:
            t["events"] = ["E"]
            t["min_interarrival"] = f"{rng.randint(2, 20) * 500}us"
        if n > 1 and rng.random() < 0.6:
            t["again_limit"] = rng.randint(1, 2)
        if rng.random() < 0.3:
            t["min_alarm_delay"] = f"{rng.randint(2, 20) * 500}us"
        if rng.random() < 0.25:                   # deadline beyond the interval
            t["deadline"] = f"{rng.randint(2, 60) * 500}us"
        tasks.append(t)
    if rng.random() < 0.6:
        tasks.append({"name": "bg", "trigger": ["background"], "run": "r",
                      "budget": f"{rng.randint(20, 400)}us"})
    return {"kernel": {"dispatch_overhead": f"{rng.randint(0, 20)}us"}, "task": tasks}


BOUND = re.compile(r"^(PASS|CONDITIONAL): R = (\d+) <=")


class Oracle(unittest.TestCase):
    def test_simulated_response_within_bound(self):
        rng = random.Random(20261018)
        checked = 0
        for case in range(150):
            doc = random_doc(rng)
            cfg = sosgen.parse(doc)
            bounds = {}
            for name, verdict in sosgen.schedulability(cfg):
                m = BOUND.match(verdict)
                if m:
                    bounds[name] = int(m.group(2))
            if not bounds:
                continue
            worst = simulate(cfg, 400_000, random.Random(case))
            for name, R in bounds.items():
                checked += 1
                self.assertLessEqual(worst[name], R,
                                     f"case {case}: task {name} observed {worst[name]} > "
                                     f"R = {R}\n{doc}")
        self.assertGreater(checked, 150)          # the check must not pass vacuously


if __name__ == "__main__":
    unittest.main()
