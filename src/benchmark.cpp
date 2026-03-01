#include "MatchingEngine.h"
#include "PoolMatchingEngine.h"
#include "ArrayMatchingEngine.h"
#include "Benchmark.h"
#include <iostream>
#include <random>
#include <vector>
#include <string>
#include <memory>

// Workload generator
// Generates a realistic mix of orders:
//   ~60% limit orders that rest (price away from mid)
//   ~30% limit orders that match (price crosses spread)
//   ~10% cancels of resting orders
//
// Mid price: $100.00 (stored as 10000 fixed-point cents)
// Spread: $0.10 ticks

struct WorkloadOrder {
    Side side;
    Price price;
    Quantity qty;
};

std::vector<WorkloadOrder> generateWorkload(std::size_t N, uint64_t seed = 42) {
    std::mt19937_64 rng(seed);

    // Price distribution: clustered around mid with some spread
    std::uniform_int_distribution<int> tickOffset(-20, 20); // +-20 ticks from mid
    std::uniform_int_distribution<int> qtyDist(1, 500);
    std::uniform_int_distribution<int> sideDist(0, 1);

    std::vector<WorkloadOrder> orders;
    orders.reserve(N);

    for (std::size_t i = 0; i < N; ++i) {
        Side side = sideDist(rng) ? Side::Buy : Side::Sell;
        int ticks = tickOffset(rng);

        // Prices cluster around $99.90–$100.10 (a realistic tight book)
        Price mid = 10000; // $100.00
        Price price = mid + ticks;
        if (price <= 0) price = 1;

        orders.push_back({side, price, static_cast<Quantity>(qtyDist(rng))});
    }
    return orders;
}

// Scenario 1: Pure insertion (no matches)
// Fill the book with resting orders on alternating sides at non-crossing prices.
// This isolates the cost of data structure insertion.
template<typename Engine>
void benchInsertOnly(Engine& engine, const std::string& label,
                     std::size_t N, std::vector<LatencyStats>& out) {

    // Use prices far apart so nothing matches
    auto stats = Benchmark::runBatch(label, N, [&](std::size_t i) {
        Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        Price price = (i % 2 == 0)
            ? static_cast<Price>(9000 - (i % 100))   // bids well below ask
            : static_cast<Price>(11000 + (i % 100));  // asks well above bid
        engine.submitOrder(side, price, 100);
    });
    out.push_back(std::move(stats));
}

// Scenario 2: Realistic mixed workload
// Prices clustered around mid so matches occur naturally.
// Most representative of real trading.
template<typename Engine>
void benchMixed(Engine& engine, const std::string& label,
                const std::vector<WorkloadOrder>& workload,
                std::vector<LatencyStats>& out) {

    auto stats = Benchmark::runBatch(label, workload.size(), [&](std::size_t i) {
        const auto& w = workload[i];
        engine.submitOrder(w.side, w.price, w.qty);
    });
    out.push_back(std::move(stats));
}

// Scenario 3: Cancel latency
// Insert N orders across a SHALLOW book (10 price levels) then time cancels.
// This is the realistic case — a real book has few active price levels.
template<typename Engine>
void benchCancel(Engine& engine, const std::string& label,
                 std::size_t N, std::vector<LatencyStats>& out) {

    // Pre-fill with orders across only 10 price levels (realistic depth)
    const int kLevels = 10;
    std::vector<OrderId> ids;
    ids.reserve(N);
    for (std::size_t i = 0; i < N; ++i) {
        Price p = static_cast<Price>(9000 - (i % kLevels)); // 10 unique prices
        engine.submitOrder(Side::Buy, p, 100);
        ids.push_back(engine.lastOrderId());
    }

    std::size_t idx = 0;
    auto stats = Benchmark::runBatch(label, N, [&](std::size_t) {
        if (idx < ids.size()) engine.cancelOrder(ids[idx++]);
    });
    out.push_back(std::move(stats));
}

// Main
int main(int argc, char* argv[]) {
    // Allow N to be passed on command line, default 100k
    std::size_t N = 100'000;
    if (argc > 1) N = static_cast<std::size_t>(std::stoull(argv[1]));

    std::cout << "\n════════════════════════════════════════════════════════════\n";
    std::cout << "  LOB Phase 2 Benchmark\n";
    std::cout << "  N = " << N << " orders per scenario\n";
    std::cout << "  Engines: Map (baseline) | Pool | Array\n";
    std::cout << "════════════════════════════════════════════════════════════\n";

    auto workload = generateWorkload(N);

    std::vector<LatencyStats> allResults;

    // Scenario 1: Insert only
    {
        auto mapEng = std::make_unique<MatchingEngine>();
        auto poolEng = std::make_unique<PoolMatchingEngine<>>();
        auto arrEng = std::make_unique<ArrayMatchingEngine>();
        benchInsertOnly(*mapEng, "Map   | insert-only", N, allResults);
        benchInsertOnly(*poolEng, "Pool | insert-only", N, allResults);
        benchInsertOnly(*arrEng, "Array | insert-only", N, allResults);
    }

    // Scenario 2: Mixed realistic workload
    {
        auto mapEng = std::make_unique<MatchingEngine>();
        auto poolEng = std::make_unique<PoolMatchingEngine<>>();
        auto arrEng = std::make_unique<ArrayMatchingEngine>();
        benchMixed(*mapEng, "Map   | mixed", workload, allResults);
        benchMixed(*poolEng, "Pool | mixed", workload, allResults);
        benchMixed(*arrEng, "Array | mixed", workload, allResults);
    }

    // Scenario 3: Cancel latency
    {
        auto mapEng = std::make_unique<MatchingEngine>();
        auto poolEng = std::make_unique<PoolMatchingEngine<>>();
        auto arrEng = std::make_unique<ArrayMatchingEngine>();
        benchCancel(*mapEng, "Map   | cancel", N, allResults);
        benchCancel(*poolEng, "Pool | cancel", N, allResults);
        benchCancel(*arrEng, "Array | cancel", N, allResults);
    }

    // Print table
    Benchmark::printTable(allResults);

    // Export CSV
    std::cout << "Exporting CSV\n";
    Benchmark::exportCSV(allResults, "lob_benchmark");
    std::cout << "\nDone\n\n";

    return 0;
}
