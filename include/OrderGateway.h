#pragma once
#include "Types.h"
#include "SPSCQueue.h"
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>
#include <vector>

// OrderRequest
// Plain data struct passed to the SPSC queue.
// 24 bytes: fits in one cache line alongside its neighbours in the ring buffer.
struct OrderRequest {
    Side side;
    Price price;
    Quantity quantity;
};

// OrderGateway
// The producer side. Runs on its own thread, generating OrderRequest and
// pushing them into the SPSCQueue for the matching thread to consume.

template<std::size_t QueueSize>
class OrderGateway {
public:
    using Queue = SPSCQueue<OrderRequest, QueueSize>;

    explicit OrderGateway(Queue& queue) : queue_(queue), running_(false), dropped_(0) {}

    ~OrderGateway() { stop(); }

    // Start the gateway with a custom feed function
    // feedFn is called repeatedly on the producer thread.
    // It should call submitOrder() to push orders, and return false when done.
    void start(std::function<bool(OrderGateway&)> feedFn) {
        running_.store(true);
        thread_ = std::thread([this, feedFn]() {
            while (running_.load()) {
                bool keepGoing = feedFn(*this);
                if (!keepGoing) break;
            }
            running_.store(false);
        });
    }

    // Start with synthetic order feed
    // Generates N orders as fast as possible, then signals done.
    void startSynthetic(const std::vector<OrderRequest>& orders) {
        running_.store(true);
        thread_ = std::thread([this, &orders]() {
            for (const auto& req : orders) {
                if (!running_.load()) break;
                // Spin-wait if queue is full
                while (running_.load() && !queue_.push(req)) {
                    std::this_thread::yield(); // drain consumer
                }
            }
            running_.store(false);
        });
    }

    // Push a single order (called from the producer thread)
    // Returns true if enqueued, false if queue full (order dropped).
    bool submitOrder(Side side, Price price, Quantity qty) {
        bool ok = queue_.push({side, price, qty});
        if (!ok) ++dropped_;
        return ok;
    }

    void stop() {
        running_.store(false);
        if (thread_.joinable()) thread_.join();
    }

    bool running() const { return running_.load(); }
    uint64_t dropped() const { return dropped_; }

private:
    Queue& queue_;
    std::atomic<bool> running_;
    std::thread thread_;
    uint64_t dropped_;
};

