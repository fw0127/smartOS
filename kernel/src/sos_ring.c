/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/*
 * Single-producer / single-consumer ring buffers (docs/03 section 6.2).
 *
 * Indices are free-running 16-bit counters; count = head - tail. Only index
 * publication happens inside the critical section. The element copy is done
 * outside it: the producer exclusively owns the slot at head until it
 * advances head, the consumer the slot at tail until it advances tail.
 */
#include "sos_kernel_pri.h"

#if SOS_CFG_RING_ENABLE

#define L_DESC(r)  (&sos_k.cfg->rings[(r)])
#define L_RT(r)    (&sos_k.ring[(r)])

static bool l_valid(sos_ring_t r)
{
    return sos_k.initialized && (r < sos_k.ring_count);
}

static uint8_t *l_slot(sos_ring_t r, uint16_t index)
{
    const sos_ring_desc_t *d = L_DESC(r);
    uint16_t pos = (uint16_t)(index & (uint16_t)(d->capacity - 1u));
    return (uint8_t *)d->storage + ((size_t)pos * (size_t)d->elem_size);
}

static void l_copy(uint8_t *dst, const uint8_t *src, uint16_t n)
{
    uint16_t i;
    for (i = 0u; i < n; i++)
    {
        dst[i] = src[i];
    }
}

#if SOS_CFG_RING_CHECK_OWNER
/* Debug check: the producer side may only be used by the producer task, or
 * outside any task when the producer is an asynchronous context; the
 * consumer side only by the consumer task (when one is declared). */
static bool l_producer_ok(sos_ring_t r)
{
    return sos_k.current == L_DESC(r)->producer;
}

static bool l_consumer_ok(sos_ring_t r)
{
    return (L_DESC(r)->consumer == SOS_TASK_NONE) || (sos_k.current == L_DESC(r)->consumer);
}
#else
#define l_producer_ok(r) true
#define l_consumer_ok(r) true
#endif

/* Producer side: find the free slot at head, or count an overflow. */
static uint8_t *l_claim(sos_ring_t r)
{
    sos_ring_rt_t *rt = L_RT(r);
    uint16_t head;
    bool full;

    sos_platform_critical_enter();
    head = rt->head;
    full = ((uint16_t)(head - rt->tail) >= L_DESC(r)->capacity);
    if (full)
    {
        if (rt->overflow_cnt != 0xFFFFu)
        {
            rt->overflow_cnt++;
        }
        rt->overflow_pending = true;        /* hook runs later in scheduler context */
        sos_k.ring_overflow_any = true;
    }
    sos_platform_critical_exit();

    if (full)
    {
        return NULL;
    }
    return l_slot(r, head);
}

/* Producer side: make the slot at head visible and notify the consumer. */
static void l_publish(sos_ring_t r)
{
    sos_ring_rt_t *rt = L_RT(r);
    const sos_ring_desc_t *d = L_DESC(r);
    uint16_t used;

    sos_platform_critical_enter();
    rt->head = (uint16_t)(rt->head + 1u);
    used = (uint16_t)(rt->head - rt->tail);
    if (used > rt->high_water)
    {
        rt->high_water = used;
    }
    sos_platform_critical_exit();

    if ((d->consumer != SOS_TASK_NONE) && (d->notify != 0u))
    {
        (void)sos_event_set_async(d->consumer, d->notify);
    }
}

/* Consumer side: the slot at tail, or NULL when empty. */
static const uint8_t *l_front(sos_ring_t r)
{
    sos_ring_rt_t *rt = L_RT(r);
    uint16_t tail;
    bool empty;

    sos_platform_critical_enter();
    tail = rt->tail;
    empty = (rt->head == tail);
    sos_platform_critical_exit();

    return empty ? NULL : l_slot(r, tail);
}

static void l_pop(sos_ring_t r)
{
    sos_platform_critical_enter();
    L_RT(r)->tail = (uint16_t)(L_RT(r)->tail + 1u);
    sos_platform_critical_exit();
}

/* ------------------------------------------------------------------------ */

sos_status_t sos_i_ring_check(const sos_cfg_root_t *root)
{
    uint8_t i;

    if (root->ring_count == 0u)
    {
        return SOS_OK;
    }
    if ((root->rings == NULL) || (root->ring_count > SOS_CFG_MAX_RINGS))
    {
        return SOS_E_CFG;
    }
    for (i = 0u; i < root->ring_count; i++)
    {
        const sos_ring_desc_t *d = &root->rings[i];
        uint16_t cap = d->capacity;

        if ((d->storage == NULL) || (d->elem_size == 0u)
            || (cap == 0u) || (cap > SOS_RING_MAX_CAP) || ((cap & (uint16_t)(cap - 1u)) != 0u)
            || ((d->notify & SOS_EV_ALARM) != 0u)
            || ((d->notify != 0u) && (d->consumer == SOS_TASK_NONE))
            || ((d->consumer != SOS_TASK_NONE) && (d->consumer >= root->task_count))
            || ((d->producer != SOS_TASK_NONE) && (d->producer >= root->task_count))
            || ((d->producer != SOS_TASK_NONE) && (d->producer == d->consumer)))
        {
            return SOS_E_CFG;
        }
    }
    return SOS_OK;
}

void sos_i_ring_reset(void)
{
    uint8_t i;

    sos_k.ring_overflow_any = false;

    for (i = 0u; i < SOS_CFG_MAX_RINGS; i++)
    {
        sos_k.ring[i].head = 0u;
        sos_k.ring[i].tail = 0u;
#if SOS_CFG_RING_ZERO_COPY
        sos_k.ring[i].reserved = false;
        sos_k.ring[i].acquired = false;
#endif
        sos_k.ring[i].high_water = 0u;
        sos_k.ring[i].overflow_cnt = 0u;
        sos_k.ring[i].overflow_pending = false;
    }
}

/* Call on_ring_overflow for every ring that overflowed since the last call:
 * once per ring, from scheduler context, however many overflows occurred
 * (the exact number is in the statistics). */
void sos_i_ring_service(void)
{
    uint8_t i;
    bool any;

    sos_platform_critical_enter();          /* written by producers, possibly async */
    any = sos_k.ring_overflow_any;
    sos_k.ring_overflow_any = false;
    sos_platform_critical_exit();
    if (!any)
    {
        return;
    }

    for (i = 0u; i < sos_k.ring_count; i++)
    {
        bool pending;

        sos_platform_critical_enter();
        pending = L_RT(i)->overflow_pending;
        L_RT(i)->overflow_pending = false;
        sos_platform_critical_exit();
        if (pending && (sos_k.hooks != NULL) && (sos_k.hooks->on_ring_overflow != NULL))
        {
            sos_k.hooks->on_ring_overflow(i);
        }
    }
}

/* An owner task is stopped or restarted: abandon what it left unfinished.
 * A producer's reserved slot is discarded (never published); a consumer's
 * acquired element stays queued and is delivered again. */
void sos_i_ring_owner_reset(sos_task_id_t id)
{
#if SOS_CFG_RING_ZERO_COPY
    uint8_t i;

    for (i = 0u; i < sos_k.ring_count; i++)
    {
        if (L_DESC(i)->producer == id)
        {
            L_RT(i)->reserved = false;
        }
        if (L_DESC(i)->consumer == id)
        {
            L_RT(i)->acquired = false;
        }
    }
#else
    (void)id;                               /* by-value access leaves nothing open */
#endif
}

sos_status_t sos_ring_put(sos_ring_t r, const void *elem)
{
    uint8_t *slot;

    if (!l_valid(r) || (elem == NULL))
    {
        return SOS_E_PARAM;
    }
    if (!l_producer_ok(r))
    {
        return SOS_E_STATE;
    }
#if SOS_CFG_RING_ZERO_COPY
    if (L_RT(r)->reserved)
    {
        return SOS_E_STATE;
    }
#endif
    slot = l_claim(r);
    if (slot == NULL)
    {
        return SOS_E_FULL;
    }
    l_copy(slot, (const uint8_t *)elem, L_DESC(r)->elem_size);
    l_publish(r);
    return SOS_OK;
}

#if SOS_CFG_RING_ZERO_COPY
void *sos_ring_reserve(sos_ring_t r)
{
    uint8_t *slot;

    if (!l_valid(r) || !l_producer_ok(r))
    {
        return NULL;
    }
    if (L_RT(r)->reserved)
    {
        return l_slot(r, L_RT(r)->head);    /* idempotent until commit */
    }
    slot = l_claim(r);
    if (slot != NULL)
    {
        L_RT(r)->reserved = true;
    }
    return slot;
}

sos_status_t sos_ring_commit(sos_ring_t r)
{
    if (!l_valid(r))
    {
        return SOS_E_PARAM;
    }
    if (!L_RT(r)->reserved || !l_producer_ok(r))
    {
        return SOS_E_STATE;
    }
    L_RT(r)->reserved = false;
    l_publish(r);
    return SOS_OK;
}
#endif

sos_status_t sos_ring_get(sos_ring_t r, void *out)
{
    const uint8_t *slot;

    if (!l_valid(r) || (out == NULL))
    {
        return SOS_E_PARAM;
    }
    if (!l_consumer_ok(r))
    {
        return SOS_E_STATE;
    }
#if SOS_CFG_RING_ZERO_COPY
    if (L_RT(r)->acquired)
    {
        return SOS_E_STATE;
    }
#endif
    slot = l_front(r);
    if (slot == NULL)
    {
        return SOS_E_EMPTY;
    }
    l_copy((uint8_t *)out, slot, L_DESC(r)->elem_size);
    l_pop(r);
    return SOS_OK;
}

sos_status_t sos_ring_peek(sos_ring_t r, void *out)
{
    const uint8_t *slot;

    if (!l_valid(r) || (out == NULL))
    {
        return SOS_E_PARAM;
    }
    if (!l_consumer_ok(r))
    {
        return SOS_E_STATE;
    }
    slot = l_front(r);
    if (slot == NULL)
    {
        return SOS_E_EMPTY;
    }
    l_copy((uint8_t *)out, slot, L_DESC(r)->elem_size);
    return SOS_OK;
}

#if SOS_CFG_RING_ZERO_COPY
const void *sos_ring_acquire(sos_ring_t r)
{
    const uint8_t *slot;

    if (!l_valid(r) || !l_consumer_ok(r))
    {
        return NULL;
    }
    slot = l_front(r);                      /* same element until release */
    if (slot != NULL)
    {
        L_RT(r)->acquired = true;
    }
    return slot;
}

sos_status_t sos_ring_release(sos_ring_t r)
{
    if (!l_valid(r))
    {
        return SOS_E_PARAM;
    }
    if (!L_RT(r)->acquired || !l_consumer_ok(r))
    {
        return SOS_E_STATE;
    }
    L_RT(r)->acquired = false;
    l_pop(r);
    return SOS_OK;
}
#endif

uint16_t sos_ring_count(sos_ring_t r)
{
    uint16_t n = 0u;

    if (l_valid(r))
    {
        sos_platform_critical_enter();
        n = (uint16_t)(L_RT(r)->head - L_RT(r)->tail);
        sos_platform_critical_exit();
    }
    return n;
}

sos_status_t sos_ring_get_stats(sos_ring_t r, sos_ring_stats_t *out)
{
    if (!l_valid(r) || (out == NULL))
    {
        return SOS_E_PARAM;
    }
    sos_platform_critical_enter();
    out->capacity = L_DESC(r)->capacity;
    out->count = (uint16_t)(L_RT(r)->head - L_RT(r)->tail);
    out->high_water = L_RT(r)->high_water;
    out->overflow_cnt = L_RT(r)->overflow_cnt;
    sos_platform_critical_exit();
    return SOS_OK;
}

#else /* !SOS_CFG_RING_ENABLE */

sos_status_t sos_i_ring_check(const sos_cfg_root_t *root)
{
    return (root->ring_count == 0u) ? SOS_OK : SOS_E_CFG;  /* rings not compiled in */
}

void sos_i_ring_reset(void)
{
}

void sos_i_ring_owner_reset(sos_task_id_t id)
{
    (void)id;
}

void sos_i_ring_service(void)
{
}

#endif
