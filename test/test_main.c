#include "test_util.h"

#define TESTS(X) \
    X(test_time_reached_wraps) X(test_time_conversion) X(test_time_elapsed_across_wrap) \
    X(test_bitmap_lowest) \
    X(test_cfg_valid) X(test_cfg_rejects) X(test_cfg_time_range) X(test_cfg_fg_limit) X(test_background_high_id) \
    X(test_errlog_ring) \
    X(test_periodic_offset) X(test_periodic_offset_across_wrap) X(test_priority_order) \
    X(test_event_preempts_between_steps) X(test_event_during_own_run) X(test_event_mask) \
    X(test_async_event_wakes_idle) X(test_event_to_periodic_only) \
    X(test_again_yield) X(test_again_yield_alone_never_waits) X(test_again_yield_to_background) \
    X(test_background_fills_idle) X(test_alarm) X(test_missed_periods) \
    X(test_periodic_and_event) X(test_stop) X(test_stats_state) \
    X(test_policy_restart_limit) X(test_policy_restart_counter_resets) X(test_policy_stop) \
    X(test_policy_ignore_no_hot_loop) X(test_policy_fatal) X(test_unknown_result) \
    X(test_init_failure_policies) X(test_restart_request) \
    RING_TESTS(X) ZC_TESTS(X) SUPERVISE_TESTS(X)

/* module-dependent groups (D12) */
#if SOS_CFG_RING_ENABLE
#define RING_TESTS(X) X(test_ring_fifo_full_empty) X(test_ring_index_wrap) \
    X(test_ring_notifies_consumer) X(test_ring_cfg_rejects)
#else
#define RING_TESTS(X) X(test_rings_disabled)
#endif
#if SOS_CFG_RING_ZERO_COPY
#define ZC_TESTS(X) X(test_ring_zero_copy) X(test_ring_owner_restart) X(test_ring_owner_stop)
#else
#define ZC_TESTS(X)
#endif
#if SOS_CFG_SUPERVISE_ENABLE
#define SUPERVISE_TESTS(X) X(test_budget_overrun) X(test_precision_warning) \
    X(test_health_while_idle) X(test_alive_starvation_stops_health) \
    X(test_alive_restart_then_fatal) X(test_alive_stop)
#else
#define SUPERVISE_TESTS(X) X(test_supervise_disabled)
#endif

#define DECLARE(name) void name(void);
TESTS(DECLARE)

#define RUN(name) do { int before = t_fail; name(); \
    printf("  %-40s %s\n", #name, (t_fail == before) ? "ok" : "FAILED"); \
    if (host_critical_depth() != 0) { printf("    FAIL: critical section left open\n"); t_fail++; } \
    } while (0);

int main(void)
{
    printf("smartOs host tests (virtual time), %d-bit ticks, ring=%d zero-copy=%d supervise=%d\n",
           SOS_PLATFORM_TICK_BITS, SOS_CFG_RING_ENABLE, SOS_CFG_RING_ZERO_COPY,
           SOS_CFG_SUPERVISE_ENABLE);
    TESTS(RUN)
    printf("%d checks, %d failed\n", t_checks, t_fail);
    return (t_fail == 0) ? 0 : 1;
}
