#pragma once
#include <cstddef>
#include <array>
#include <stdexcept>
#include <cstdint>

// PoolAllocator<T, Capacity>
// A fixed-size object pool that allocates T objects from a contiguous arena.
//
// Design goal:
//  - Zero heap allocations after construction
//  - 0(1) alloc and free via free-list
//  - All objects in a single continous block, cache friendly
//  - Not thread-safe
//
// How it works:
//  The arena is an array of Slots. Each Slot is an union of either a T (when
//  allocated) or a uint32_t pointing to the next free Slot (when free).
//  On construction every slot is linked to a free list.
//  alloc() pops the head of the free list and placement-new's a T there.
//  free() destruct T and pushes the slot back onte the free list.

template<typename T, std::size_t Capacity>
class PoolAllocator {
public:
    static constexpr std::size_t kCapacity = Capacity;

    PoolAllocator() {
        // Build the free list: slot i points to slot i+1
        for (std::size_t i = 0; i < Capacity - 1; ++i) {
            slots_[i].next = static_cast<uint32_t>(i + 1);
        }
        slots_[Capacity - 1].next = kNull;
        freeHead_ = 0;
        allocated_ = 0;
    }

    // Allocate one T, forwarding constructor arguments
    template<typename... Args>
    T* alloc(Args&&... args) {
        if (freeHead_ == kNull) {
            throw std::runtime_error("PoolAllocator exhausted");
        }
        uint32_t idx = freeHead_;
        freeHead_ = slots_[idx].next;
        ++allocated_;
        return new (&slots_[idx].storage) T(std::forward<Args>(args)...);
    }

    // Return a T back to the pool
    void free(T* ptr) {
        ptr->~T();
        // Recover the slot index from the pointer
        auto* slot = reinterpret_cast<Slot*>(ptr);
        uint32_t idx = static_cast<uint32_t>(slot - slots_.data());
        slot->next = freeHead_;
        freeHead_ = idx;
        --allocated_;
    }

    std::size_t allocated() const { return allocated_; }
    std::size_t available() const { return Capacity - allocated_; }

private:
    static constexpr uint32_t kNull = UINT32_MAX;

    // Each slot is either a live T or a free-list link, never both
    union Slot {
        typename std::aligned_storage<sizeof(T), alignof(T)>::type storage;
        uint32_t next;
        Slot() : next(0) {}
    };

    std::array<Slot, Capacity> slots_;
    uint32_t freeHead_;
    std::size_t allocated_;
};

