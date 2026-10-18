#include "counter-state.hpp"
#include <atomic>

static std::atomic<uint64_t> g_counter{0};

uint64_t counter_get()
{
    return g_counter.load(std::memory_order_relaxed);
}

void counter_set(uint64_t value)
{
    g_counter.store(value, std::memory_order_relaxed);
}

uint64_t counter_add(int64_t delta)
{
    if (delta >= 0)
        return g_counter.fetch_add((uint64_t)delta, std::memory_order_relaxed) + (uint64_t)delta;

    const uint64_t amount = (uint64_t)(-delta);
    uint64_t current = g_counter.load(std::memory_order_relaxed);
    for (;;) {
        const uint64_t next = current > amount ? current - amount : 0;
        if (g_counter.compare_exchange_weak(current, next, std::memory_order_relaxed))
            return next;
    }
}
