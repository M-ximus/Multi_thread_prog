#pragma once

#include <atomic>
#include <stdint.h>
#include <assert.h>
#include <emmintrin.h>
#include <sched.h>
#include <time.h>

// Base class
class spinlock
{
public:

    virtual void lock() = 0;
    virtual void unlock() = 0;

};

class spinlock_TAS: public spinlock
{
private:

    std::atomic_uint8_t mem;

public:

    spinlock_TAS(): mem(0) {};
    ~spinlock_TAS() {assert(mem.load() == 0);};

    void lock() override;
    void unlock() override;

};

class spinlock_TTAS: public spinlock
{
private:

    std::atomic_uint8_t mem;

public:

    spinlock_TTAS(): mem(0) {};
    ~spinlock_TTAS() {assert(mem.load() == 0);};

    void lock() override;
    void unlock() override;

};

class ticket_lock: public spinlock
{
private:

    std::atomic_size_t queue;
    std::atomic_size_t dequeue;

public:

    ticket_lock(): queue(0), dequeue(0) {};
    ~ticket_lock() {assert(queue.load() == dequeue.load());};

    void lock() override;
    void unlock() override;
};
