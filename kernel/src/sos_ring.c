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

static void l_overflow(sos_ring_t r)
{
    if ((sos_k.hooks != NULL) && (sos_k.hooks->on_ring_overflow != NULL))
    {
        sos_k.hooks->on_ring_overflow(r);
    }
}

/* Producer side: find the free slot at head, or count an overflow. */
static uint8_t *l_claim(sos_ring_t r)
{
    sos_ring_rt_t *rt = L_RT(r);
    uint16_t head;
    bool full;

    sos_platform_critical_enter();
    head = rt->head;
    full = ((uint16_t)(head - rt->tail) >= L_DESC(r)->capacity);
    if (full && (rt->overflow_cnt != 0xFFFFu))
    {
        rt->overflow_cnt++;
    }
    sos_platform_critical_exit();

    if (full)
    {
        l_overflow(r);
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
            || ((d->notify != 0u) && (d->consumer >= root->task_count)))
        {
            return SOS_E_CFG;
        }
    }
    return SOS_OK;
}

void sos_i_ring_reset(void)
{
    uint8_t i;

    for (i = 0u; i < SOS_CFG_MAX_RINGS; i++)
    {
        sos_k.ring[i].head = 0u;
        sos_k.ring[i].tail = 0u;
        sos_k.ring[i].reserved = false;
        sos_k.ring[i].acquired = false;
        sos_k.ring[i].high_water = 0u;
        sos_k.ring[i].overflow_cnt = 0u;
    }
}

sos_status_t sos_ring_put(sos_ring_t r, const void *elem)
{
    uint8_t *slot;

    if (!l_valid(r) || (elem == NULL))
    {
        return SOS_E_PARAM;
    }
    if (L_RT(r)->reserved)
    {
        return SOS_E_STATE;
    }
    slot = l_claim(r);
    if (slot == NULL)
    {
        return SOS_E_FULL;
    }
    l_copy(slot, (const uint8_t *)elem, L_DESC(r)->elem_size);
    l_publish(r);
    return SOS_OK;
}

void *sos_ring_reserve(sos_ring_t r)
{
    uint8_t *slot;

    if (!l_valid(r))
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
    if (!L_RT(r)->reserved)
    {
        return SOS_E_STATE;
    }
    L_RT(r)->reserved = false;
    l_publish(r);
    return SOS_OK;
}

sos_status_t sos_ring_get(sos_ring_t r, void *out)
{
    const uint8_t *slot;

    if (!l_valid(r) || (out == NULL))
    {
        return SOS_E_PARAM;
    }
    if (L_RT(r)->acquired)
    {
        return SOS_E_STATE;
    }
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
    slot = l_front(r);
    if (slot == NULL)
    {
        return SOS_E_EMPTY;
    }
    l_copy((uint8_t *)out, slot, L_DESC(r)->elem_size);
    return SOS_OK;
}

const void *sos_ring_acquire(sos_ring_t r)
{
    const uint8_t *slot;

    if (!l_valid(r))
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
    if (!L_RT(r)->acquired)
    {
        return SOS_E_STATE;
    }
    L_RT(r)->acquired = false;
    l_pop(r);
    return SOS_OK;
}

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
