# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""sosgen: smartOs configuration generator (docs/06).

Reads a TOML description file, checks it, and writes
  sos_cfg.h       kernel compile-time parameters
  sos_cfg_gen.h   named ids, event bits, ring handles, callback prototypes
  sos_cfg_gen.c   sorted task table, ring storage, hook table, sos_cfg_root
plus a text report (id/bit assignment, schedulability estimate).

Python 3.11+, standard library only. Output is deterministic.
"""

from __future__ import annotations

import json
import math
import re
import sys
import tomllib
import zlib
from dataclasses import dataclass, field
from pathlib import Path

GEN_VERSION = (0, 5, 2)
ABI_VERSION = 3
FG_MAX = 32
RING_MAX_CAP = 32768
EVENTS_PER_TASK = 31              # bit 31 is SOS_EV_ALARM

IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
TIME = re.compile(r"^\s*(\d+)\s*(ns|us|ms|s)\s*$")
NS_PER = {"ns": 1, "us": 1_000, "ms": 1_000_000, "s": 1_000_000_000}
POLICIES = {"ignore": "SOS_ON_ERR_IGNORE", "restart": "SOS_ON_ERR_RESTART",
            "stop": "SOS_ON_ERR_STOP", "fatal": "SOS_ON_ERR_FATAL"}
HOOKS = ("on_overrun", "on_task_error", "on_fatal", "on_idle", "on_ring_overflow")
HOOK_PROTOS = {
    "on_overrun": "void {}(sos_task_id_t id, sos_tick_t step)",
    "on_task_error": "void {}(sos_task_id_t id, sos_status_t code)",
    "on_fatal": "void {}(sos_status_t code)",
    "on_idle": "void {}(void)",
    "on_ring_overflow": "void {}(uint8_t ring)",
}
KERNEL_KEYS = {"time_unit_us", "errlog_depth", "overrun_is_error",
               "health_enable", "health_min_interval",
               "ring_enable", "ring_zero_copy", "supervise_enable", "dispatch_overhead"}
TASK_KEYS = {"name", "prio", "trigger", "period", "offset", "events", "budget",
             "alive_window", "again_limit", "on_error", "max_restart",
             "min_interarrival", "max_steps", "deadline", "init", "run", "done", "ctx"}
RING_KEYS = {"name", "elem_type", "capacity", "producer", "consumer", "notify"}
TOP_KEYS = {"kernel", "hooks", "task", "ring", "app"}


class ConfigError(Exception):
    """A description-file error; the message names the file and entry."""


@dataclass
class Task:
    name: str
    prio: int | None
    triggers: list[str]
    period: int = 0
    offset: int = 0
    budget: int = 0
    alive_window: int = 0
    again_limit: int = 0
    on_error: str = "fatal"
    max_restart: int = 0
    min_interarrival: int = 0
    max_steps: int = 0                  # 0 = not given (analysis assumes 1)
    deadline: int = 0                   # 0 = equal to the release interval
    events: list[str] = field(default_factory=list)
    init: str | None = None
    run: str = ""
    done: str | None = None
    ctx: str | None = None
    tid: int = -1

    @property
    def background(self) -> bool:
        return self.triggers == ["background"]

    def bit(self, event: str) -> int:
        return 1 << self.events.index(event)


@dataclass
class Ring:
    name: str
    elem_type: str
    capacity: int
    producer: str
    consumer: str
    notify: str | None
    rid: int = -1


@dataclass
class Config:
    source: str
    time_unit_us: int
    errlog_depth: int
    overrun_is_error: bool
    health_enable: bool
    health_min_interval: int
    ring_enable: bool
    ring_zero_copy: bool
    supervise_enable: bool
    dispatch_overhead: int
    hooks: dict[str, str]
    tasks: list[Task]               # sorted: foreground by prio, then background
    rings: list[Ring]
    headers: list[str]
    digest: int = 0
    warnings: list[str] = field(default_factory=list)


@dataclass
class Platform:
    tick_bits: int
    ticks_per_sec: int


# --------------------------------------------------------------------------
# parsing and checks
# --------------------------------------------------------------------------

class _Ctx:
    def __init__(self, source: str):
        self.source = source

    def fail(self, where: str, msg: str) -> None:
        raise ConfigError(f"{self.source}: {where}: {msg}")


def _ident(c: _Ctx, where: str, key: str, v) -> str:
    if not isinstance(v, str) or not IDENT.match(v):
        c.fail(where, f"'{key}' must be a C identifier, got {v!r}")
    return v


def _uint(c: _Ctx, where: str, key: str, v, hi: int) -> int:
    if isinstance(v, bool) or not isinstance(v, int) or v < 0 or v > hi:
        c.fail(where, f"'{key}' must be an integer in 0..{hi}, got {v!r}")
    return v


def _time(c: _Ctx, where: str, key: str, v, unit_us: int) -> int:
    """Time string -> configuration units (check 6: must divide exactly)."""
    if not isinstance(v, str):
        c.fail(where, f"'{key}' must be a string with a unit (ns/us/ms/s), got {v!r}")
    m = TIME.match(v)
    if not m:
        c.fail(where, f"'{key}' = {v!r}: expected e.g. \"10ms\", \"50us\"")
    ns = int(m.group(1)) * NS_PER[m.group(2)]
    unit_ns = unit_us * 1000
    if ns % unit_ns:
        c.fail(where, f"'{key}' = {v!r} is not a whole number of {unit_us} us units")
    units = ns // unit_ns
    if units > 0xFFFFFFFF:
        c.fail(where, f"'{key}' = {v!r} exceeds 32-bit configuration time")
    return units


def _unknown(c: _Ctx, where: str, table: dict, allowed: set[str]) -> None:
    extra = sorted(set(table) - allowed)
    if extra:
        c.fail(where, f"unknown key(s): {', '.join(extra)}")


def load(path: str | Path) -> Config:
    path = Path(path)
    with open(path, "rb") as f:
        try:
            doc = tomllib.load(f)
        except tomllib.TOMLDecodeError as e:
            raise ConfigError(f"{path.name}: {e}") from None
    return parse(doc, path.name)


def parse(doc: dict, source: str = "<config>") -> Config:
    c = _Ctx(source)
    _unknown(c, "top level", doc, TOP_KEYS)

    k = doc.get("kernel", {})
    _unknown(c, "[kernel]", k, KERNEL_KEYS)
    unit_us = _uint(c, "[kernel]", "time_unit_us", k.get("time_unit_us", 1), 1_000_000)
    if unit_us == 0 or 1_000_000 % unit_us:
        c.fail("[kernel]", "'time_unit_us' must divide 1000000")
    errlog = _uint(c, "[kernel]", "errlog_depth", k.get("errlog_depth", 8), 1024)
    if errlog < 1:
        c.fail("[kernel]", "'errlog_depth' must be >= 1")
    for b in ("overrun_is_error", "health_enable", "ring_enable", "ring_zero_copy",
              "supervise_enable"):
        if b in k and not isinstance(k[b], bool):
            c.fail("[kernel]", f"'{b}' must be true or false")
    health_iv = _time(c, "[kernel]", "health_min_interval",
                      k.get("health_min_interval", "10ms"), unit_us)
    if health_iv == 0:
        c.fail("[kernel]", "'health_min_interval' must be > 0")

    hooks_t = doc.get("hooks", {})
    _unknown(c, "[hooks]", hooks_t, set(HOOKS))
    hooks = {h: _ident(c, "[hooks]", h, hooks_t[h]) for h in HOOKS if h in hooks_t}

    app = doc.get("app", {})
    _unknown(c, "[app]", app, {"headers"})
    headers = app.get("headers", [])
    if not isinstance(headers, list) or not all(isinstance(h, str) and h for h in headers):
        c.fail("[app]", "'headers' must be a list of file names")

    tasks = [_parse_task(c, i, t, unit_us) for i, t in enumerate(doc.get("task", []))]
    if not tasks:
        c.fail("[[task]]", "at least one task is required")
    _check_names(c, "task", [t.name for t in tasks])

    # check 1: unique priorities among foreground tasks; sort and assign ids
    fg = [t for t in tasks if not t.background]
    bg = [t for t in tasks if t.background]
    seen: dict[int, str] = {}
    for t in fg:
        if t.prio in seen:
            c.fail(f"task '{t.name}'", f"prio {t.prio} already used by task '{seen[t.prio]}'")
        seen[t.prio] = t.name
    if len(fg) > FG_MAX:
        c.fail("[[task]]", f"{len(fg)} foreground tasks; at most {FG_MAX}")
    if len(tasks) > 254:
        c.fail("[[task]]", "at most 254 tasks")
    ordered = sorted(fg, key=lambda t: t.prio) + bg      # bg keep file order
    for i, t in enumerate(ordered):
        t.tid = i
    by_name = {t.name: t for t in ordered}

    rings = [_parse_ring(c, i, r, by_name) for i, r in enumerate(doc.get("ring", []))]
    _check_names(c, "ring", [r.name for r in rings])
    if len(rings) > 254:
        c.fail("[[ring]]", "at most 254 rings")
    for i, r in enumerate(rings):
        r.rid = i

    # optional modules (D12): rings default to "compiled in iff declared"
    ring_enable = k.get("ring_enable", bool(rings))
    ring_zc = k.get("ring_zero_copy", ring_enable)
    supervise = k.get("supervise_enable", True)
    if rings and not ring_enable:
        c.fail("[kernel]", "'ring_enable' is false but [[ring]] entries are declared")
    if ring_zc and not ring_enable:
        c.fail("[kernel]", "'ring_zero_copy' requires 'ring_enable'")
    if not supervise:
        for t in ordered:
            if t.alive_window:
                c.fail(f"task '{t.name}'", "'alive_window' requires supervise_enable = true")
        for key in ("health_enable", "overrun_is_error"):
            if k.get(key):
                c.fail("[kernel]", f"'{key}' requires supervise_enable = true")

    overhead = _time(c, "[kernel]", "dispatch_overhead", k.get("dispatch_overhead", "0us"),
                     unit_us)
    cfg = Config(source, unit_us, errlog, bool(k.get("overrun_is_error", False)),
                 bool(k.get("health_enable", True)), health_iv, ring_enable, ring_zc,
                 supervise, overhead, hooks, ordered, rings, headers)
    cfg.digest = _digest(doc)
    return cfg


def _parse_task(c: _Ctx, i: int, t: dict, unit_us: int) -> Task:
    where = f"task[{i}]"
    if not isinstance(t, dict):
        c.fail(where, "must be a table")
    _unknown(c, where, t, TASK_KEYS)
    name = _ident(c, where, "name", t.get("name"))
    where = f"task[{i}] '{name}'"

    trig = t.get("trigger")
    if not isinstance(trig, list) or not trig or not all(isinstance(x, str) for x in trig):
        c.fail(where, "'trigger' must be a non-empty list of \"periodic\", \"event\", \"background\"")
    trig = sorted(set(trig))
    # check 2: legal combinations
    if any(x not in ("periodic", "event", "background") for x in trig):
        c.fail(where, f"unknown trigger in {trig}")
    if "background" in trig and len(trig) > 1:
        c.fail(where, "'background' cannot be combined with other triggers")
    background = trig == ["background"]

    if background:
        for key in ("prio", "period", "offset", "events", "alive_window", "again_limit",
                    "min_interarrival", "max_steps", "deadline"):
            if key in t:
                c.fail(where, f"'{key}' is not allowed for a background task")
        prio = None
    else:
        if "prio" not in t:
            c.fail(where, "'prio' is required for a foreground task")
        prio = _uint(c, where, "prio", t["prio"], 255)

    task = Task(name=name, prio=prio, triggers=trig)
    for key in ("period", "offset", "budget", "alive_window", "min_interarrival", "deadline"):
        if key in t:
            setattr(task, key, _time(c, where, key, t[key], unit_us))
    if "max_steps" in t:
        task.max_steps = _uint(c, where, "max_steps", t["max_steps"], 0xFFFF)
        if task.max_steps == 0:
            c.fail(where, "'max_steps' must be >= 1")
    task.again_limit = _uint(c, where, "again_limit", t.get("again_limit", 0), 0xFFFF)
    task.max_restart = _uint(c, where, "max_restart", t.get("max_restart", 0), 255)
    pol = t.get("on_error", "fatal")
    if pol not in POLICIES:
        c.fail(where, f"'on_error' must be one of {', '.join(POLICIES)}, got {pol!r}")
    task.on_error = pol

    # check 3: trigger requirements and events
    if "periodic" in trig and task.period == 0:
        c.fail(where, "a periodic task needs 'period' > 0")
    if "periodic" not in trig and "period" in t:
        c.fail(where, "'period' given but trigger is not periodic")
    ev = t.get("events", [])
    if not isinstance(ev, list):
        c.fail(where, "'events' must be a list of names")
    for e in ev:
        _ident(c, where, "events", e)
    if len(set(ev)) != len(ev):
        c.fail(where, "duplicate event names")
    if len(ev) > EVENTS_PER_TASK:
        c.fail(where, f"{len(ev)} events; at most {EVENTS_PER_TASK} per task")
    if "event" in trig and not ev:
        c.fail(where, "an event-triggered task needs at least one entry in 'events'")
    task.events = list(ev)

    task.run = _ident(c, where, "run", t.get("run"))
    for key in ("init", "done", "ctx"):
        if key in t:
            setattr(task, key, _ident(c, where, key, t[key]))
    return task


def _parse_ring(c: _Ctx, i: int, r: dict, tasks: dict[str, Task]) -> Ring:
    where = f"ring[{i}]"
    if not isinstance(r, dict):
        c.fail(where, "must be a table")
    _unknown(c, where, r, RING_KEYS)
    name = _ident(c, where, "name", r.get("name"))
    where = f"ring[{i}] '{name}'"
    elem = r.get("elem_type")
    if not isinstance(elem, str) or not re.match(r"^[A-Za-z_][A-Za-z0-9_ ]*$", elem):
        c.fail(where, f"'elem_type' must be a C type name, got {elem!r}")
    cap = _uint(c, where, "capacity", r.get("capacity"), RING_MAX_CAP)
    # check 4: capacity, producer, consumer, notify
    if cap == 0 or cap & (cap - 1):
        c.fail(where, f"'capacity' must be a power of two in 1..{RING_MAX_CAP}, got {cap}")
    prod = r.get("producer")
    cons = r.get("consumer")
    if prod != "async" and prod not in tasks:
        c.fail(where, f"'producer' must be \"async\" or a task name, got {prod!r}")
    if cons not in tasks:
        c.fail(where, f"'consumer' must be a task name, got {cons!r}")
    # check 5: one producer, one consumer, not the same task
    if prod == cons:
        c.fail(where, "producer and consumer must differ")
    notify = r.get("notify")
    if notify is not None and notify not in tasks[cons].events:
        c.fail(where, f"'notify' = {notify!r} is not an event declared by consumer '{cons}'")
    return Ring(name, elem.strip(), cap, prod, cons, notify)


def _check_names(c: _Ctx, kind: str, names: list[str]) -> None:
    seen = set()
    for n in names:
        if n.upper() in seen:
            c.fail(f"[[{kind}]]", f"duplicate {kind} name '{n}' (names are case-insensitive)")
        seen.add(n.upper())


def _digest(doc: dict) -> int:
    canon = json.dumps(doc, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
    return zlib.crc32(canon.encode("ascii")) & 0xFFFFFFFF


# --------------------------------------------------------------------------
# platform-dependent checks (7, 8) and schedulability estimate (9)
# --------------------------------------------------------------------------

def check_platform(cfg: Config, p: Platform) -> None:
    if p.tick_bits not in (16, 32, 64) or p.ticks_per_sec <= 0:
        raise ConfigError("platform profile: tick bits must be 16/32/64, rate > 0")
    units_per_sec = 1_000_000 // cfg.time_unit_us
    limit = (1 << (p.tick_bits - 1)) // 2

    def ticks(units: int) -> int:
        return (units * p.ticks_per_sec + units_per_sec // 2) // units_per_sec

    items = [("[kernel]", "health_min_interval", cfg.health_min_interval)]
    for t in cfg.tasks:
        for key in ("period", "offset", "budget", "alive_window"):
            items.append((f"task '{t.name}'", key, getattr(t, key)))
    for where, key, units in items:
        if units and ticks(units) >= limit:
            raise ConfigError(f"{cfg.source}: {where}: '{key}' is {ticks(units)} ticks; the "
                              f"{p.tick_bits}-bit time source allows < {limit}")
    for t in cfg.tasks:
        if t.budget and ticks(t.budget) < 10:
            cfg.warnings.append(f"task '{t.name}': budget is {ticks(t.budget)} ticks; "
                                f"resolution coarser than budget/10")


def _streams(t: Task) -> list[int] | None:
    """Release streams of a task (D17): a periodic stream (period) and an
    event stream (min_interarrival). They are independent, so their releases
    ADD UP: in a window R a task is released at most sum(ceil(R / T)) times
    -- taking only the smaller interval would undercount (an event just
    before and another just after a periodic release). None = rate unknown."""
    out = []
    if "periodic" in t.triggers:
        out.append(t.period)
    if "event" in t.triggers:
        if not t.min_interarrival:
            return None                 # an event task without a bound on its arrivals
        out.append(t.min_interarrival)
    return out or None


def _releases(streams: list[int], window: int) -> int:
    return sum(math.ceil(window / T) for T in streams)


def _steps(t: Task) -> int:
    return t.max_steps if t.max_steps else 1


# verdict classes
PASS, CONDITIONAL, FAIL, NA = "PASS", "CONDITIONAL", "FAIL", "N/A"


def schedulability(cfg: Config) -> list[tuple[str, str]]:
    """Non-preemptive fixed-priority response time of one job (docs/03 section 12).

    A job is one release of a task and may take several steps (AGAIN):
      C_i = budget + dispatch_overhead (per step)   n_i = max_steps (default 1)
      releases_j(R) = sum over j's release streams of ceil(R / T)
      D_i = deadline (default: the shortest release interval of i)
      B_i = longest step of any lower-priority or background task
      k_i = 1 + (n_i - 1) // again_limit_i     (each yield admits one lower step)
      R_i = k_i * B_i + n_i * C_i + sum_hp releases_j(R_i) * n_j * C_j
    Verdicts: PASS (all inputs given, R <= D), CONDITIONAL (R <= D, but some
    max_steps was assumed to be 1), FAIL (R > D), N/A (inputs missing)."""
    ov = cfg.dispatch_overhead
    fg = [t for t in cfg.tasks if not t.background]
    rows = []
    for i, t in enumerate(fg):
        streams = _streams(t)
        if not t.budget or streams is None:
            why = ("an event-triggered task needs min_interarrival"
                   if t.budget and "event" in t.triggers else "needs budget and a release rate")
            rows.append((t.name, f"{NA} ({why})"))
            continue
        hp = fg[:i]
        if any(not h.budget or _streams(h) is None for h in hp):
            rows.append((t.name, f"{NA} (a higher-priority task lacks budget or release rate)"))
            continue
        lower = [x.budget for x in cfg.tasks[i + 1:]]
        if any(b == 0 for b in lower):
            rows.append((t.name, f"{NA} (a lower-priority or background task lacks budget)"))
            continue
        D = t.deadline or min(streams)
        n = _steps(t)
        k = 1 + ((n - 1) // t.again_limit if t.again_limit else 0)
        B = max(lower, default=0) + (ov if lower else 0)
        C = t.budget + ov
        R = k * B + n * C
        while True:
            Rn = k * B + n * C + sum(_releases(_streams(h), R) * _steps(h) * (h.budget + ov)
                                     for h in hp)
            if Rn == R or Rn > 100 * D:
                break
            R = Rn
        R = Rn
        assumed = [x.name for x in [t] + hp if not x.max_steps]
        if R > D:
            cls, rel = FAIL, ">"
        else:
            cls, rel = (CONDITIONAL if assumed else PASS), "<="
        note = f"  [assumes 1 step per job for: {', '.join(assumed)}]" if assumed else ""
        rows.append((t.name, f"{cls}: R = {R} {rel} D = {D}{note}"))
    return rows


# --------------------------------------------------------------------------
# output
# --------------------------------------------------------------------------

def _notice(cfg: Config, what: str) -> str:
    v = ".".join(map(str, GEN_VERSION))
    return (f"/* {what}\n"
            f" * Generated by sosgen {v} from {cfg.source}. Do not edit.\n"
            f" * This file is part of the user's configuration and is not covered by\n"
            f" * the MPL-2.0 license of smartOs. */\n")


def _gen_version() -> int:
    return (GEN_VERSION[0] << 8) | GEN_VERSION[1]


def render_cfg_h(cfg: Config) -> str:
    out = [_notice(cfg, "smartOs kernel parameters."),
           "#ifndef SOS_CFG_H", "#define SOS_CFG_H", "",
           f"#define SOS_TIME_UNITS_PER_SEC      {1_000_000 // cfg.time_unit_us}u",
           f"#define SOS_CFG_MAX_TASKS           {len(cfg.tasks)}u",
           f"#define SOS_CFG_MAX_RINGS           {max(1, len(cfg.rings))}u",
           f"#define SOS_CFG_ERRLOG_DEPTH        {cfg.errlog_depth}u",
           f"#define SOS_CFG_OVERRUN_IS_ERROR    {int(cfg.overrun_is_error)}",
           f"#define SOS_CFG_HEALTH_ENABLE       {int(cfg.health_enable)}",
           f"#define SOS_CFG_HEALTH_MIN_INTERVAL {cfg.health_min_interval}u",
           f"#define SOS_CFG_RING_ENABLE         {int(cfg.ring_enable)}",
           f"#define SOS_CFG_RING_ZERO_COPY      {int(cfg.ring_zero_copy)}",
           f"#define SOS_CFG_SUPERVISE_ENABLE    {int(cfg.supervise_enable)}",
           "", "#endif", ""]
    return "\n".join(out)


def render_gen_h(cfg: Config) -> str:
    out = [_notice(cfg, "smartOs configuration: names and prototypes."),
           "#ifndef SOS_CFG_GEN_H", "#define SOS_CFG_GEN_H", "",
           '#include "sos_pub.h"', "",
           f"#define SOS_CFG_GEN_VERSION  0x{_gen_version():04X}u",
           f"#define SOS_CFG_DIGEST       0x{cfg.digest:08X}u", "",
           "/* tasks */"]
    for t in cfg.tasks:
        out.append(f"#define SOS_TASK_{t.name.upper():<20} ((sos_task_id_t){t.tid}u)")
    out.append("")
    out.append("/* events */")
    for t in cfg.tasks:
        for e in t.events:
            out.append(f"#define SOS_EV_{(t.name + '_' + e).upper():<24} "
                       f"((sos_events_t)0x{t.bit(e):08X}u)")
    if cfg.rings:
        out.append("")
        out.append("/* ring buffers */")
        for r in cfg.rings:
            out.append(f"#define SOS_RING_{r.name.upper():<20} ((sos_ring_t){r.rid}u)")
    out += ["", "extern const sos_cfg_root_t sos_cfg_root;", "",
            "/* initialise with the header/table pairing check (D14) */",
            "#define SOS_CFG_INIT() sos_init_checked(&sos_cfg_root, SOS_CFG_DIGEST)", "",
            "/* callbacks referenced by the configuration */"]
    protos = []
    for t in cfg.tasks:
        if t.init:
            protos.append(f"sos_status_t {t.init}(void *ctx);")
        protos.append(f"sos_run_result_t {t.run}(void *ctx, sos_events_t events);")
        if t.done:
            protos.append(f"void {t.done}(void *ctx);")
    for h, fn in cfg.hooks.items():
        protos.append(HOOK_PROTOS[h].format(fn) + ";")
    seen = set()
    for p in protos:
        if p not in seen:
            out.append(p)
            seen.add(p)
    out += ["", "#endif", ""]
    return "\n".join(out)


def render_gen_c(cfg: Config) -> str:
    out = [_notice(cfg, "smartOs configuration tables."), '#include "sos_cfg_gen.h"']
    out += [f'#include "{h}"' for h in cfg.headers]
    out.append("")

    for r in cfg.rings:
        out.append(f"static {r.elem_type} l_ring_{r.name}[{r.capacity}];")
    if cfg.rings:
        out.append("")

    def trig(t: Task) -> str:
        names = {"periodic": "SOS_TRIG_PERIODIC", "event": "SOS_TRIG_EVENT",
                 "background": "SOS_TRIG_BACKGROUND"}
        return " | ".join(names[x] for x in t.triggers)

    out.append("static const sos_task_desc_t l_tasks[] =\n{")
    for t in cfg.tasks:
        mask = sum(t.bit(e) for e in t.events)
        out += [
            "    {",
            f'        .name = "{t.name}",',
            f"        .prio = {t.prio if t.prio is not None else 0}u,",
            f"        .trigger = {trig(t)},",
            f"        .on_error = {POLICIES[t.on_error]},",
            f"        .max_restart = {t.max_restart}u,",
            f"        .again_limit = {t.again_limit}u,",
            f"        .period = {t.period}u,",
            f"        .offset = {t.offset}u,",
            f"        .budget = {t.budget}u,",
            f"        .alive_window = {t.alive_window}u,",
            f"        .event_mask = 0x{mask:08X}u,",
            f"        .init = {t.init or 'NULL'},",
            f"        .run = {t.run},",
            f"        .done = {t.done or 'NULL'},",
            f"        .ctx = {('&' + t.ctx) if t.ctx else 'NULL'},",
            "    },",
        ]
    out.append("};\n")

    by_name = {t.name: t for t in cfg.tasks}
    if cfg.rings:
        out.append("static const sos_ring_desc_t l_rings[] =\n{")
        for r in cfg.rings:
            cons = by_name[r.consumer]
            notify = f"0x{cons.bit(r.notify):08X}u" if r.notify else "0u"
            producer = ("SOS_TASK_NONE" if r.producer == "async"
                        else f"SOS_TASK_{r.producer.upper()}")
            out += [
                "    {",
                f'        .name = "{r.name}",',
                f"        .storage = l_ring_{r.name},",
                f"        .elem_size = (uint16_t)sizeof({r.elem_type}),",
                f"        .capacity = {r.capacity}u,",
                f"        .producer = {producer},",
                f"        .consumer = SOS_TASK_{cons.name.upper()},",
                f"        .notify = {notify},",
                "    },",
            ]
        out.append("};\n")

    out.append("static const sos_hooks_t l_hooks =\n{")
    for h in HOOKS:
        out.append(f"    .{h} = {cfg.hooks.get(h, 'NULL')},")
    out.append("};\n")

    out += [
        "const sos_cfg_root_t sos_cfg_root =",
        "{",
        f"    .abi_version = {ABI_VERSION}u,",
        "    .gen_version = SOS_CFG_GEN_VERSION,",
        f"    .digest = 0x{cfg.digest:08X}u,       /* literal: checked against the header */",
        "    .tasks = l_tasks,",
        f"    .task_count = {len(cfg.tasks)}u,",
        f"    .rings = {'l_rings' if cfg.rings else 'NULL'},",
        f"    .ring_count = {len(cfg.rings)}u,",
        "    .hooks = &l_hooks,",
        "};",
        "",
    ]
    return "\n".join(out)


def render_report(cfg: Config, platform: Platform | None) -> str:
    unit = f"{cfg.time_unit_us} us" if cfg.time_unit_us != 1 else "us"
    lines = [f"sosgen {'.'.join(map(str, GEN_VERSION))} report for {cfg.source}",
             f"digest 0x{cfg.digest:08X}, time unit {unit}", "",
             "id  bit  prio  task                 trigger            period    budget    events"]
    for t in cfg.tasks:
        bit = str(t.tid) if not t.background else "-"
        prio = str(t.prio) if t.prio is not None else "-"
        lines.append(f"{t.tid:<3} {bit:<4} {prio:<5} {t.name:<20} {'+'.join(t.triggers):<18} "
                     f"{t.period or '-':<9} {t.budget or '-':<9} {', '.join(t.events) or '-'}")
    if cfg.rings:
        lines += ["", "id  ring                 elem_type            cap   producer -> consumer (notify)"]
        for r in cfg.rings:
            lines.append(f"{r.rid:<3} {r.name:<20} {r.elem_type:<20} {r.capacity:<5} "
                         f"{r.producer} -> {r.consumer} ({r.notify or '-'})")
    lines += ["", "schedulability (non-preemptive fixed priority, design-time estimate):",
              f"  per-step kernel overhead included: {cfg.dispatch_overhead or 0} "
              f"(set [kernel] dispatch_overhead from a target measurement)",
              "  NOT included: release scan and supervision passes, platform wait/wake-up",
              "  latency, interrupt handlers, and anything outside the task steps.",
              "  PASS = all inputs given and R <= D; CONDITIONAL = holds only under the noted",
              "  assumptions; FAIL = R > D; N/A = inputs missing. Budgets are assumptions",
              "  to be checked against target measurements, not proven WCETs."]
    for name, verdict in schedulability(cfg):
        lines.append(f"  {name:<20} {verdict}")
    if platform:
        lines += ["", f"platform: {platform.tick_bits}-bit ticks at {platform.ticks_per_sec} Hz"]
    for w in cfg.warnings:
        lines.append(f"warning: {w}")
    return "\n".join(lines) + "\n"


def generate(desc: str | Path, outdir: str | Path,
             platform: Platform | None = None) -> str:
    cfg = load(desc)
    if platform:
        check_platform(cfg, platform)
    out = Path(outdir)
    out.mkdir(parents=True, exist_ok=True)
    files = {
        "sos_cfg.h": render_cfg_h(cfg),
        "sos_cfg_gen.h": render_gen_h(cfg),
        "sos_cfg_gen.c": render_gen_c(cfg),
    }
    for name, text in files.items():
        p = out / name
        if not p.exists() or p.read_text() != text:   # keep mtimes stable
            p.write_text(text)
    return render_report(cfg, platform)


def main(argv: list[str] | None = None) -> int:
    import argparse
    ap = argparse.ArgumentParser(prog="sosgen", description="smartOs configuration generator")
    ap.add_argument("description", help="TOML description file")
    ap.add_argument("-o", "--outdir", required=True, help="output directory")
    ap.add_argument("--tick-bits", type=int, help="platform time-source width (16/32/64)")
    ap.add_argument("--ticks-per-sec", type=int, help="platform time-source rate")
    ap.add_argument("--report", help="write the report to this file instead of stdout")
    ap.add_argument("--version", action="version",
                    version="sosgen " + ".".join(map(str, GEN_VERSION)))
    a = ap.parse_args(argv)
    if (a.tick_bits is None) != (a.ticks_per_sec is None):
        ap.error("--tick-bits and --ticks-per-sec go together")
    platform = Platform(a.tick_bits, a.ticks_per_sec) if a.tick_bits else None
    try:
        report = generate(a.description, a.outdir, platform)
    except ConfigError as e:
        print(f"sosgen: error: {e}", file=sys.stderr)
        return 1
    if a.report:
        Path(a.report).write_text(report)
    else:
        sys.stdout.write(report)
    return 0
