# Changelog

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

