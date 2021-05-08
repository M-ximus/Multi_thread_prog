#include "spinlocks.hpp"

////////////////////////////////////////////////////////////////////////////////

void spinlock_TAS::lock()
{
    uint8_t expected_zero;
    size_t curr_attempt = 0;
    do
    {
        curr_attempt++;

        if (curr_attempt != 1)
            sched_yield();

        expected_zero = 0;
    } while(!mem.compare_exchange_weak(expected_zero, 1, std::memory_order_acquire));
}

void spinlock_TAS::unlock()
{
    mem.store(0, std::memory_order_release);
}

////////////////////////////////////////////////////////////////////////////////

void spinlock_TTAS::lock()
{
    uint8_t expected_zero;
    do
    {
        size_t curr_attempt = 0;
        expected_zero = 0;
        while(mem.load(std::memory_order_relaxed))
        {
            if (curr_attempt != 0)
                sched_yield();

            curr_attempt++;
        };

    } while(!mem.compare_exchange_weak(expected_zero, 1, std::memory_order_acquire));
}

void spinlock_TTAS::unlock()
{
    mem.store(0, std::memory_order_release);
}

////////////////////////////////////////////////////////////////////////////////

void ticket_lock::lock()
{
    size_t curr_attempt = 0;
    const auto ticket = queue.fetch_add(1, std::memory_order_acquire);
    while(ticket != dequeue.load(std::memory_order_relaxed))
    {
        curr_attempt++;
        if (curr_attempt != 0)
            sched_yield();
    }
}

void ticket_lock::unlock()
{
    const auto curr = dequeue.load(std::memory_order_relaxed) + 1;
    dequeue.store(curr, std::memory_order_release);
}
