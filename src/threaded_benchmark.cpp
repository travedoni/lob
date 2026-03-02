#include "ThreadedEngine.h"
#include "Benchmark.h"
#include <iostream>
#include <iomanip>
#include <random>
#include <vector>
#include <string>
#include <memory>

// Workload Generator
std::vector<OrderRequest> generateOrders(std::size_t N, uint64_t seed = 42) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> tickDist(-15, 15);
    std::uniform_int_distribution<int> qtyDist(1, 200);
    std::uniform_int_distribution<int> sideDist(0, 1);

    std::vector<OrderRequest> orders;
    orders.reserve(N);

    for (std::size_t i = 0; i < N; ++i) {
        Side  side  = sideDist(rng) ? Side::Buy : Side::Sell;
        Price price = 10000 + tickDist(rng); // clustered around $100
        orders.push_back({side, price, static_cast<Quantity>(qtyDist(rng))});
    }
    return orders;
}

// Scenario 1: Throughput test
// Push N orders as fast as possible. Measures end-to-end pipeline throughput.
void benchThroughput(std::size_t N) {
    std::cout << "\n[1/2] Throughput — " << N << " orders through full pipeline\n";

    auto orders = generateOrders(N);
    auto engine = std::make_unique<ThreadedEngine<65536, 1000000>>();

    engine->start();
    auto elapsed = engine->runSynthetic(orders);
    engine->stop();

    double elapsedMs = elapsed.count() / 1e6;
    double throughput = N / (elapsed.count() / 1e9);

    std::cout << "  Orders sent     : " << N << "\n";
    std::cout << "  Orders matched  : " << engine->ordersProcessed() << "\n";
    std::cout << "  Dropped         : " << engine->droppedOrders() << "\n";
    std::cout << "  Wall time       : " << std::fixed << std::setprecision(2)
              << elapsedMs << " ms\n";
    std::cout << "  Throughput      : " << std::fixed << std::setprecision(0)
              << throughput / 1e6 << "M orders/sec\n";

    engine->logger().printLatencySummary();
    engine->logger().exportCSV("threaded_trades.csv");
}

// Scenario 2: Queue backpressure test
// Use a tiny queue (16 slots) to force the gateway to spin-wait.
// Shows how the system behaves under back-pressure.
void benchBackpressure(std::size_t N) {
    std::cout << "\n[2/2] Backpressure — tiny queue (16 slots), " << N << " orders\n";

    auto orders = generateOrders(N, 99); // different seed

    // Queue of only 16 slots, gateway will hit it constantly
    auto engine = std::make_unique<ThreadedEngine<16, 1000000>>();

    engine->start();
    auto elapsed = engine->runSynthetic(orders);
    engine->stop();

    double elapsedMs = elapsed.count() / 1e6;
    double throughput = N / (elapsed.count() / 1e9);

    std::cout << "  Orders sent     : " << N << "\n";
    std::cout << "  Orders matched  : " << engine->ordersProcessed() << "\n";
    std::cout << "  Dropped         : " << engine->droppedOrders() << "\n";
    std::cout << "  Wall time       : " << std::fixed << std::setprecision(2)
              << elapsedMs << " ms\n";
    std::cout << "  Throughput      : " << std::fixed << std::setprecision(0)
              << throughput / 1e6 << "M orders/sec\n";

    engine->logger().printLatencySummary();
}

// Main
int main(int argc, char* argv[]) {
    std::size_t N = 200'000;
    if (argc > 1) N = static_cast<std::size_t>(std::stoull(argv[1]));

    std::cout << "\n════════════════════════════════════════════════════════════\n";
    std::cout <<   "  LOB Phase 3 — Threaded Engine Benchmark\n";
    std::cout <<   "  SPSC lock-free queue + Pool matching engine\n";
    std::cout <<   "════════════════════════════════════════════════════════════\n";

    benchThroughput(N);
    benchBackpressure(N);

    std::cout << "\nDone.\n\n";
    return 0;
}
