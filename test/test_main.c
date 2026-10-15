#include "test_util.h"

#define TESTS(X) \
    X(test_time_reached_wraps) X(test_time_conversion) X(test_time_elapsed_across_wrap) \
    X(test_bitmap_lowest) \
    X(test_cfg_valid) X(test_cfg_rejects) X(test_cfg_time_range) X(test_cfg_fg_limit) \
    X(test_errlog_ring) \
    X(test_periodic_offset) X(test_periodic_offset_across_wrap) X(test_priority_order) \
    X(test_event_preempts_between_steps) X(test_event_during_own_run) X(test_event_mask) \
    X(test_async_event_wakes_idle) \
    X(test_again_yield) X(test_again_yield_alone_never_waits) X(test_again_yield_to_background) \
    X(test_background_fills_idle) X(test_alarm) X(test_missed_periods) \
    X(test_periodic_and_event) X(test_stop) X(test_stats_state) \
    X(test_policy_restart_limit) X(test_policy_restart_counter_resets) X(test_policy_stop) \
    X(test_policy_ignore_no_hot_loop) X(test_policy_fatal) X(test_unknown_result) \
    X(test_init_failure_policies) X(test_restart_request)

#define DECLARE(name) void name(void);
TESTS(DECLARE)

#define RUN(name) do { int before = t_fail; name(); \
    printf("  %-40s %s\n", #name, (t_fail == before) ? "ok" : "FAILED"); \
    if (host_critical_depth() != 0) { printf("    FAIL: critical section left open\n"); t_fail++; } \
    } while (0);

int main(void)
{
    printf("smartOs v0.1 host tests, %d-bit ticks\n", SOS_PLATFORM_TICK_BITS);
    TESTS(RUN)
    printf("%d checks, %d failed\n", t_checks, t_fail);
    return (t_fail == 0) ? 0 : 1;
}
