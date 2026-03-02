#include "../include/SPSCQueue.h"
#include "../include/ThreadedEngine.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>
#include <numeric>
#include <chrono>

static int passed = 0;
static int failed = 0;

#define TEST(name) std::cout << "  " << name << " ... "
#define PASS() do { std::cout << "\033[32mPASS\033[0m\n"; ++passed; } while(0)
#define FAIL(msg) do { std::cout << "\033[31mFAIL: " << msg << "\033[0m\n"; ++failed; } while(0)
#define ASSERT(cond, msg) do { if(cond) PASS(); else FAIL(msg); } while(0)

// SPSCQueue Tests

void testQueueEmptyOnConstruct() {
    TEST("Queue is empty on construction");
    SPSCQueue<int, 8> q;
    ASSERT(q.empty() && q.size() == 0, "should be empty");
}

void testQueuePushPop() {
    TEST("Push and pop single item");
    SPSCQueue<int, 8> q;
    q.push(42);
    auto val = q.pop();
    ASSERT(val.has_value() && *val == 42, "should pop 42");
}

void testQueueFIFOOrder() {
    TEST("Queue preserves FIFO order");
    SPSCQueue<int, 8> q;
    q.push(1); q.push(2); q.push(3);
    bool ok = (*q.pop() == 1) && (*q.pop() == 2) && (*q.pop() == 3);
    ASSERT(ok, "should pop in insertion order");
}

void testQueueFullReturnsFalse() {
    TEST("Push to full queue returns false");
    SPSCQueue<int, 4> q; // capacity = 3 (one slot reserved)
    q.push(1); q.push(2); q.push(3);
    bool rejected = !q.push(4);
    ASSERT(rejected, "should reject when full");
}

void testQueueEmptyReturnsNullopt() {
    TEST("Pop from empty queue returns nullopt");
    SPSCQueue<int, 8> q;
    auto val = q.pop();
    ASSERT(!val.has_value(), "should return nullopt");
}

void testQueueWrapAround() {
    TEST("Queue wraps around correctly");
    SPSCQueue<int, 4> q; // capacity = 3
    q.push(1); q.pop();
    q.push(2); q.pop();
    q.push(3); q.pop();
    q.push(4); // should wrap to slot 0
    auto val = q.pop();
    ASSERT(val.has_value() && *val == 4, "should wrap around correctly");
}

void testQueueConcurrentProducerConsumer() {
    TEST("Concurrent push/pop produces correct results (1M items)");

    constexpr std::size_t N = 1'000'000;
    SPSCQueue<uint64_t, 65536> q;

    uint64_t checksum_sent = 0;
    uint64_t checksum_recv = 0;

    // Producer thread
    std::thread producer([&]() {
        for (uint64_t i = 0; i < N; ++i) {
            while (!q.push(i)) std::this_thread::yield(); // spin if full
            checksum_sent += i;
        }
    });

    // Consumer thread (this thread)
    uint64_t received = 0;
    while (received < N) {
        auto val = q.pop();
        if (val) {
            checksum_recv += *val;
            ++received;
        }
    }

    producer.join();
    ASSERT(checksum_sent == checksum_recv, "checksums must match, no lost/duplicated items");
}

// ThreadedEngine Tests

void testThreadedEngineProcessesOrders() {
    TEST("ThreadedEngine processes all submitted orders");

    constexpr std::size_t N = 10'000;
    std::vector<OrderRequest> orders;
    orders.reserve(N);
    for (std::size_t i = 0; i < N; ++i) {
        Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        Price price = 10000 + static_cast<Price>(i % 10) - 5;
        orders.push_back({side, price, 100});
    }

    auto engine = std::make_unique<ThreadedEngine<65536, 100000>>();
    engine->start();
    engine->runSynthetic(orders);
    engine->stop();

    ASSERT(engine->ordersProcessed() == N, "all orders should be processed");
}

void testThreadedEngineProducesTradesOnCross() {
    TEST("ThreadedEngine generates trades when prices cross");

    // Alternate buy/sell at same price so every pair matches
    std::vector<OrderRequest> orders;
    for (int i = 0; i < 100; ++i) {
        orders.push_back({Side::Buy, 10000, 10});
        orders.push_back({Side::Sell, 10000, 10});
    }

    auto engine = std::make_unique<ThreadedEngine<65536, 100000>>();
    engine->start();
    engine->runSynthetic(orders);
    engine->stop();

    ASSERT(engine->logger().tradeCount() > 0, "should have executed trades");
}

void testThreadedEngineDropsOnTinyQueue() {
    TEST("Dropped orders counted correctly on tiny queue under load");

    // 16-slot queue, fast producer, slow consumer → some drops expected
    // Just verify the accounting is correct (dropped + processed = sent)
    constexpr std::size_t N = 5000;
    std::vector<OrderRequest> orders(N, {Side::Buy, 10000, 1});

    auto engine = std::make_unique<ThreadedEngine<16, 100000>>();
    engine->start();
    engine->runSynthetic(orders);
    engine->stop();

    uint64_t total = engine->ordersProcessed() + engine->droppedOrders();
    ASSERT(total == N, "processed + dropped should equal total sent");
}

// Runner
int main() {
    std::cout << "\n════════════════════════════════════════\n";
    std::cout << "Test Suite — SPSC Queue & Threaded Engine\n";
    std::cout << "═════════════════════════════════════════\n\n";

    std::cout << "SPSCQueue:\n";
    testQueueEmptyOnConstruct();
    testQueuePushPop();
    testQueueFIFOOrder();
    testQueueFullReturnsFalse();
    testQueueEmptyReturnsNullopt();
    testQueueWrapAround();
    testQueueConcurrentProducerConsumer();

    std::cout << "\nThreadedEngine:\n";
    testThreadedEngineProcessesOrders();
    testThreadedEngineProducesTradesOnCross();
    testThreadedEngineDropsOnTinyQueue();

    std::cout << "\n────────────────────────────────────────────────\n";
    std::cout << "  Results: " << passed << " passed, " << failed << " failed\n";
    std::cout << "────────────────────────────────────────────────\n\n";

    return failed > 0 ? 1 : 0;
}
