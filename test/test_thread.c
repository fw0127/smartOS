#define _POSIX_C_SOURCE 200809L
/*
 * Concurrency tests on the host thread platform: a producer thread acts as an
 * asynchronous context (interrupt) while the scheduler runs on the main thread.
 *
 *  1. by-value ring: N sequence numbers, consumer drains K per step
 *  2. zero-copy ring: 64-byte records written/read in place, checksummed
 *  3. idle wake-up: events posted while the kernel sleeps wake it promptly
 *
 * Built and run by `make test-thread` (and `make test-tsan` under ThreadSanitizer).
 */
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "sos_pub.h"
#include "thread_platform.h"

static int g_fail;
#define CHECK(c) do { if (!(c)) { g_fail++; printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define N_VALUE   300000u
#define N_RECORD  100000u
#define N_WAKE    200u
#define K_PER_STEP 32u

enum { T_CONSUMER = 0 };
#define EV_DATA 0x1u

/* ---- shared ------------------------------------------------------------- */
static uint32_t        g_vals[64];
typedef struct { uint32_t seq; uint32_t data[15]; } record_t;
static record_t        g_recs[16];

static sos_ring_desc_t g_rings[2];
static int             g_mode;              /* 1 value, 2 record, 3 wake */
static atomic_uint     g_produced_full;     /* FULL / NULL results seen by producer */
static atomic_bool     g_done;

/* consumer-side (scheduler thread) */
static uint32_t g_expect;
static uint32_t g_errors;
static uint32_t g_steps;

/* wake test */
static atomic_ullong   g_post_at;           /* tick of the latest post */
static uint64_t        g_max_latency;
static uint32_t        g_wakes;

static sos_run_result_t consumer_run(void *ctx, sos_events_t ev)
{
    uint32_t k;
    (void)ctx;
    (void)ev;
    g_steps++;

    if (g_mode == 3)
    {
        uint64_t p = atomic_load(&g_post_at);
        uint64_t lat = (p != 0u) ? ((uint64_t)sos_time_now() - p) : 0u;
        if (lat > g_max_latency) { g_max_latency = lat; }
        atomic_store(&g_post_at, 0u);            /* lets the producer post the next one */
        g_wakes++;
        if (g_wakes == N_WAKE) { atomic_store(&g_done, true); }
        return SOS_RUN_OK;
    }

    for (k = 0u; k < K_PER_STEP; k++)
    {
        if (g_mode == 1)
        {
            uint32_t v;
            if (sos_ring_get(0u, &v) != SOS_OK) { break; }
            if (v != g_expect) { g_errors++; }
        }
        else
        {
            const record_t *r = (const record_t *)sos_ring_acquire(1u);
            uint32_t i;
            if (r == NULL) { break; }
            if (r->seq != g_expect) { g_errors++; }
            for (i = 0u; i < 15u; i++)
            {
                if (r->data[i] != r->seq * 31u + i) { g_errors++; }
            }
            (void)sos_ring_release(1u);
        }
        g_expect++;
    }
    if (g_expect == ((g_mode == 1) ? N_VALUE : N_RECORD))
    {
        atomic_store(&g_done, true);
    }
    return (sos_ring_count((g_mode == 1) ? 0u : 1u) != 0u) ? SOS_RUN_AGAIN : SOS_RUN_OK;
}

static void *producer(void *arg)
{
    uint32_t i;
    (void)arg;

    if (g_mode == 1)
    {
        for (i = 0u; i < N_VALUE; i++)
        {
            while (sos_ring_put(0u, &i) == SOS_E_FULL)
            {
                atomic_fetch_add(&g_produced_full, 1u);
                (void)sched_yield();
            }
        }
    }
    else if (g_mode == 2)
    {
        for (i = 0u; i < N_RECORD; i++)
        {
            record_t *r;
            uint32_t j;
            while ((r = (record_t *)sos_ring_reserve(1u)) == NULL)
            {
                atomic_fetch_add(&g_produced_full, 1u);
                (void)sched_yield();
            }
            r->seq = i;
            for (j = 0u; j < 15u; j++) { r->data[j] = i * 31u + j; }
            (void)sos_ring_commit(1u);
        }
    }
    else
    {
        struct timespec ms = { 0, 1000000L };
        for (i = 0u; i < N_WAKE; i++)
        {
            (void)nanosleep(&ms, NULL);          /* the kernel is asleep by now */
            atomic_store(&g_post_at, (unsigned long long)sos_time_now() | 1u); /* never 0 */
            (void)sos_event_set_async(T_CONSUMER, EV_DATA);
            while (atomic_load(&g_post_at) != 0u) { (void)sched_yield(); } /* one at a time */
        }
    }
    return NULL;
}

static const sos_task_desc_t g_tasks[] =
{
    { "consumer", 0u, SOS_TRIG_EVENT, SOS_ON_ERR_FATAL, 0u, 0u, 0u, 0u, 0u, 0u,
      EV_DATA, NULL, consumer_run, NULL, NULL },
};

static int run_mode(int mode, const char *name)
{
    pthread_t th;
    sos_cfg_root_t root;
    sos_ring_stats_t st;
    uint64_t t_end;
    int before = g_fail;

    memset(&root, 0, sizeof root);
    root.abi_version = SOS_ABI_VERSION;
    root.tasks = g_tasks;
    root.task_count = 1u;
    g_rings[0] = (sos_ring_desc_t){ "values", g_vals, sizeof(uint32_t), 64u, T_CONSUMER, EV_DATA };
    g_rings[1] = (sos_ring_desc_t){ "records", g_recs, sizeof(record_t), 16u, T_CONSUMER, EV_DATA };
    root.rings = g_rings;
    root.ring_count = 2u;

    g_mode = mode;
    g_expect = 0u;
    g_errors = 0u;
    g_steps = 0u;
    g_wakes = 0u;
    g_max_latency = 0u;
    atomic_store(&g_done, false);
    atomic_store(&g_produced_full, 0u);
    atomic_store(&g_post_at, 0u);

    CHECK(sos_init(&root) == SOS_OK);
    CHECK(pthread_create(&th, NULL, producer, NULL) == 0);

    t_end = (uint64_t)sos_time_now() + 30000000u;          /* 30 s guard */
    while (!atomic_load(&g_done) && ((uint64_t)sos_time_now() < t_end))
    {
        sos_tick_t dl;
        if (sos_poll(&dl) == SOS_POLL_IDLE)
        {
            sos_platform_wait_until(dl);
        }
    }
    if (mode == 3) { atomic_store(&g_post_at, 0u); }
    (void)pthread_join(th, NULL);

    CHECK(atomic_load(&g_done));
    CHECK(g_errors == 0u);
    if (mode != 3)
    {
        CHECK(sos_ring_get_stats((mode == 1) ? 0u : 1u, &st) == SOS_OK);
        CHECK(st.overflow_cnt == ((atomic_load(&g_produced_full) > 0xFFFFu)
                                  ? 0xFFFFu : atomic_load(&g_produced_full)));
        printf("  %-28s ok=%s  items=%u steps=%u full-retries=%u high-water=%u\n",
               name, (g_fail == before) ? "yes" : "NO", g_expect, g_steps,
               atomic_load(&g_produced_full), st.high_water);
    }
    else
    {
        CHECK(g_wakes == N_WAKE);
        CHECK(g_max_latency < 20000u);                       /* < 20 ms even on a busy host */
        printf("  %-28s ok=%s  wakes=%u max-latency=%llu us\n",
               name, (g_fail == before) ? "yes" : "NO", g_wakes,
               (unsigned long long)g_max_latency);
    }
    return g_fail == before;
}

int main(void)
{
    thread_platform_init();
    printf("smartOs host tests (threads, real time)\n");
    (void)run_mode(1, "ring by value");
    (void)run_mode(2, "ring zero copy");
    (void)run_mode(3, "async wake-up from idle");
    printf("%d failed\n", g_fail);
    return (g_fail == 0) ? 0 : 1;
}
