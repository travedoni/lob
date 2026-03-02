#pragma once
#include "Types.h"
#include "SPSCQueue.h"
#include "OrderGateway.h"
#include "PoolMatchingEngine.h"
#include "TradeLogger.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <iostream>

// ThreadedEngine
// Owns the full producer -> queue -> consumer pipeline.
//
// Architecture:
//
//   ┌─────────────────────┐         ┌──────────────────────────┐
//   │   OrderGateway      │         │    Matching Thread       │
//   │   (producer)        │         │    (consumer)            │
//   │                     │         │                          │
//   │  submitOrder()  ────┼──push──►│  pop() → match()         │
//   │                     │         │       → TradeLogger      │
//   └─────────────────────┘         └──────────────────────────┘
//              │                                │
//              └──────── SPSCQueue ─────────────┘
//                        (lock-free)
//
// Template parameters:
//   QueueSize: ring buffer capacity (must be power of 2, default 64k slots)
//   PoolSize: order pool capacity (default 1M orders)

template<
    std::size_t QueueSize = 65536, // 64k slots
    std::size_t PoolSize = 1'000'000
>
class ThreadedEngine {
public:
    using Queue = SPSCQueue<OrderRequest, QueueSize>;

    ThreadedEngine()
        : gateway_(queue_)
        , matcherRunning_(false)
        , ordersProcessed_(0)
    {}

    ~ThreadedEngine() { stop(); }

    // Start both threads
    void start() {
        matcherRunning_.store(true);
        matcherThread_ = std::thread(&ThreadedEngine::matcherLoop, this);
    }

    // Run a synthetic benchmark feed
    // Pushes `orders` from the gateway thread, waits for matching to finish.
    // Returns wall-clock duration of the full pipeline.
    std::chrono::nanoseconds runSynthetic(const std::vector<OrderRequest>& orders) {
        auto t0 = now();
        gateway_.startSynthetic(orders);

        // Wait for gateway to finish producting
        while (gateway_.running()) std::this_thread::yield();

        // Wait until matcher has processed every order that was sent.
        const uint64_t expected = orders.size() - gateway_.dropped();
        while (ordersProcessed_.load() < expected) std::this_thread::yield();

        auto t1 = now();
        return t1 - t0;
    }

    void stop() {
        gateway_.stop();
        matcherRunning_.store(false);
        if (matcherThread_.joinable()) matcherThread_.join();
    }

    TradeLogger& logger() { return logger_; }
    uint64_t ordersProcessed() const { return ordersProcessed_; }
    uint64_t droppedOrders() const { return gateway_.dropped(); }

private:
    // Matching thread pool (consumer)
    // Spins on the queue. When an order arrives, matches it immediatly.
    void matcherLoop() {
        while (matcherRunning_.load()) {
            auto req = queue_.pop();
            if (!req) {
                std::this_thread::yield();
                continue;
            }

            uint64_t receivedNs = now().time_since_epoch().count();
            auto trades = engine_.submitOrder(req->side, req->price, req->quantity);
            uint64_t executedNs = now().time_since_epoch().count();

            for (const auto& trade : trades) {
                logger_.record(trade, receivedNs, executedNs);
            }

            ++ordersProcessed_;
        }
    }

    static std::chrono::steady_clock::time_point now() {
        return std::chrono::steady_clock::now();
    }

    Queue queue_;
    OrderGateway<QueueSize> gateway_;
    PoolMatchingEngine<PoolSize> engine_;
    TradeLogger logger_;

    std::thread matcherThread_;
    std::atomic<bool> matcherRunning_;
    std::atomic<uint64_t> ordersProcessed_;
};
