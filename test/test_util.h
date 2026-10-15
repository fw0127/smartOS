/* Minimal test harness and scripted tasks for host tests. */
#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdio.h>
#include "sos_pub.h"
#include "host_platform.h"

extern int t_fail;
extern int t_checks;

#define CHECK(c) do { t_checks++; if (!(c)) { t_fail++; \
    printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define CHECK_EQ(a, b) do { unsigned long long a_ = (unsigned long long)(a); \
    unsigned long long b_ = (unsigned long long)(b); t_checks++; \
    if (a_ != b_) { t_fail++; printf("    FAIL %s:%d: %s == %s (%llu != %llu)\n", \
    __FILE__, __LINE__, #a, #b, a_, b_); } } while (0)

/* Run stmt expecting sos_platform_halt; evaluates to the halt reason or ~0u. */
#define EXPECT_HALT(stmt, reason_out) do { host_halted = false; \
    if (setjmp(host_halt_jb) == 0) { host_halt_armed = true; stmt; host_halt_armed = false; } \
    (reason_out) = host_halted ? host_halt_reason : 0xFFFFFFFFu; } while (0)

/* ---- scripted task ------------------------------------------------------ */
typedef struct tt tt_t;
struct tt
{
    uint64_t                cost;          /* virtual ticks consumed per step */
    const sos_run_result_t *script;        /* results per step, then dflt */
    uint32_t                script_len;
    sos_run_result_t        dflt;
    uint32_t                init_failures; /* fail the first n init calls */
    void                  (*action)(tt_t *self, sos_events_t ev);
    /* observed */
    uint32_t                runs;
    uint32_t                inits;
    uint32_t                dones;
    sos_events_t            last_ev;
    uint64_t                last_run_at;   /* relative to trace start */
    int                     user;          /* free for actions */
};

sos_status_t     tt_init(void *ctx);
sos_run_result_t tt_run(void *ctx, sos_events_t ev);
void             tt_done(void *ctx);

sos_task_desc_t tdesc(const char *name, uint8_t prio, uint8_t trigger,
                      sos_time_t period, sos_time_t offset, sos_events_t mask,
                      tt_t *ctx);
sos_cfg_root_t  troot(const sos_task_desc_t *tasks, uint8_t n);

/* ---- trace: (task, time relative to trace_reset) ------------------------ */
#define TRACE_MAX 4096u
typedef struct { uint8_t task; uint64_t at; } trace_t;
extern trace_t  g_trace[TRACE_MAX];
extern unsigned g_ntrace;

void     trace_reset(void);
uint64_t rel_now(void);

/* Drive the kernel like sos_start for 'duration' ticks of virtual time. */
void run_for(uint64_t duration);

/* Reset host + test state; start time may be near the tick wrap. */
void fresh(uint64_t start_abs);

/* tick value just below the wrap point of the configured width */
uint64_t near_wrap(uint64_t below);

#endif
