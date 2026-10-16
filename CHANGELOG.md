# Changelog

## v0.5.2 — third review round

- Fix (D16): `init` and `done` ran with the identity of whichever task
  caused them (or none), so `sos_task_self()` was wrong there and an alarm
  set in `init` while task A restarted task B landed on A. Lifecycle
  callbacks now run as their own task. Stop/restart of another task
  requested from task context is carried out after the current step, so one
  task's step never contains another task's `init`/`done`.
- Fix (D17): `sosgen` merged the periodic and event release streams with
  min(), which undercounts (events just before and after a periodic
  release). Streams now add up; an event task without `min_interarrival`
  gives N/A. Verdicts PASS / CONDITIONAL / FAIL / N/A; optional
  `[kernel] dispatch_overhead`; the report states what it does not include.
- `max_job_steps` statistic to check the `max_steps` assumption on target.
- Example: worker deadline from the request queue depth (3.6 ms); under the
  stricter model its response bound is 1.32 ms, above the 1.2 ms interval.
- Release tags v0.1.0 ... v0.5.2.

## v0.5.1 — second review round (unreleased)

- Fix (D13): a yield was consumed by any dispatch, so a higher-priority task
  that happened to be ready at the yield took the lower task's opportunity.
  Now only a lower foreground step or the idle slot consumes it.
- Fix: `sosgen` response-time estimate used the period even when events
  could arrive more often, and counted one step per release. Now per job:
  T = min(period, min_interarrival), new `max_steps` and `deadline`, extra
  blocking per yield; missing inputs give n/a; assumptions are printed.
- D14: `sos_init_checked()` and generated `SOS_CFG_INIT()` reject a header
  and table from different generator runs; the generator version is now
  documented as informative only.
- D15: `on_ring_overflow` runs in scheduler context (coalesced) instead of
  possibly inside an interrupt.
- Fix: `sos_ticks_to_time` could wrap in a 64-bit multiplication before
  saturating.
- `SOS_CFG_RING_CHECK_OWNER` debug check; API call-context matrix (docs/04).
- Randomised state-machine tests for scheduler and rings with invariants;
  `docs/07` maps each guarantee to implementation, tests and target checks.

## v0.5.0 — review fixes (unreleased)

- Platform contract: the critical section must also be callable from an
  asynchronous context without blocking, and acts as a full memory barrier
  (the kernel relied on both; now stated, and the contract suite enters it
  nested from the asynchronous context).
- Ring ownership (D11): `sos_ring_desc_t` gains `producer`; `consumer` is
  always the consumer task. Stopping or restarting an owner task abandons
  its unfinished reserve (slot discarded) or acquire (element redelivered).
  Previously a task restarted between reserve and commit got `SOS_E_STATE`
  from every later `put`. Configuration ABI 3.
- Optional modules (D12): `SOS_CFG_RING_ENABLE`, `SOS_CFG_RING_ZERO_COPY`,
  `SOS_CFG_SUPERVISE_ENABLE`; `sosgen` `[kernel]` keys with consistency
  checks; `make test` builds and runs four module combinations.
- Documented: strict-priority starvation is by design and detected by alive
  supervision; budgets are after-the-fact; "one producer" means one
  execution context.
- Fix: gcc `-Wtype-limits` errors with 16- and 64-bit ticks.

## v0.4.0 — platform contract conformance suite (unreleased)

- `platform/contract_tests`: platform-independent conformance checks of the
  seven contract functions (time, critical section, wait, sticky notify,
  health), driven by a small per-platform harness; harnesses for the
  virtual-time and thread platforms. Mutation-checked: removing notify
  stickiness or critical-section exclusion makes the suite fail.
- CMake convenience target (`smartos_add_kernel`), GitHub Actions workflow
  (Ubuntu gcc/clang, macOS clang; not yet run on Linux).
- POSIX feature macro for the thread test sources (glibc portability).

## v0.3.0 — configuration generator (unreleased)

- `tools/sosgen`: TOML description file -> `sos_cfg.h`, `sos_cfg_gen.h`,
  `sos_cfg_gen.c` and a report (D4, D9). Sorts tasks by priority, assigns
  ids/bits/ring handles, emits callback prototypes, digest and generator
  version; deterministic output; generated files declare they are the
  user's and not covered by the MPL-2.0.
- Build-time checks 1-9 of docs/06, optional platform profile for range and
  precision checks, non-preemptive response-time estimate.
- `examples/layered_pipeline`: two-stage pipeline generated from app.toml,
  run end to end on the virtual platform (`make test-gen`).
- Kernel: events wake only EVENT-triggered tasks; other tasks receive the
  bits with their next step.

## v0.2.0 — ring buffers and supervision (unreleased)

- `sos_ring`: single-producer/single-consumer ring buffers, by value
  (`put/get/peek`) and zero-copy (`reserve/commit`, `acquire/release`) (D8);
  optional consumer notification; overflow counting and hook; statistics.
- Supervision: step budgets with overrun hook (optionally an error), alive
  windows with error policies, health signal gated by alive state, running
  also while idle; precision warning for budgets below 10 ticks.
- Configuration ABI 2: `rings` / `ring_count` in `sos_cfg_root_t`.
- Host thread platform (POSIX): real time, other threads as asynchronous
  contexts; concurrency tests, also under ThreadSanitizer.
- Fix: background tasks with id >= 32 no longer shift past the ready bitmap.
- LICENSE now carries the verbatim MPL-2.0 text.

## v0.1.0 — kernel skeleton (unreleased)

- Cooperative run-to-completion scheduler: ready bitmap with constant-time
  lowest-bit lookup (D1), `AGAIN` yield after `again_limit` steps (D3),
  background tasks in the idle slot, periodic release without drift and with
  missed-period counting, one-shot alarms.
- Events: per-task bits, async-safe posting with platform notify.
- Error policies IGNORE / RESTART / STOP / FATAL, error log ring, single
  fatal exit through `sos_platform_halt`.
- Hooks via function-pointer table (D5).
- `sos_poll` for embedding in an existing main loop (D10).
- Platform contract `platform/sos_platform.h`; host virtual-time platform.
- Host tests for 16/32/64-bit tick widths, also under ASan/UBSan.
- Hand-written configuration example `examples/manual_cfg`.

