#pragma once
#include "Types.h"
#include <vector>
#include <fstream>
#include <string>
#include <chrono>
#include <mutex>
#include <numeric>
#include <algorithm>
#include <iomanip>
#include <iostream>

// TradeRecord
// Extended trade record with timestamps for latency measurement.
// order_received_ns: when the order entered the queue (gateway side)
// trade_executed_ns: when the match completed (matching thread side)
// latency_ns = trade_executed_ns - order_received_ns
//            = full pipeline latency: queue wait + matching time
struct TradeRecord {
    OrderId makerOrderId;
    OrderId takerOrderId;
    Price price;
    Quantity quantity;
    uint64_t order_received_ns;
    uint64_t trade_executed_ns;
    uint64_t latency_ns;
};

// TradeLogger
// Collects trades from the matching thread and exports them to CSV.
// Uses a mutex because there may eventually have multiple matching
// threads writing trades. For single matching thread, the mutex is uncontested
// and nearly free.
class TradeLogger {
public:
    void record(const Trade& trade, uint64_t receivedNs, uint64_t executedNs) {
        std::lock_guard<std::mutex> lock(mutex_);
        records_.push_back({
            trade.makerOrderId,
            trade.takerOrderId,
            trade.price,
            trade.quantity,
            receivedNs,
            executedNs,
            executedNs - receivedNs
        });
    }

    // Export to CSV
    void exportCSV(const std::string& path) const {
        std::ofstream f(path);
        f << "maker_id,taker_id,price,quantity,received_ns,executed_ns,latency_ns\n";
        for (const auto& r : records_) {
            f << r.makerOrderId << ","
              << r.takerOrderId << ","
              << std::fixed << std::setprecision(2) << r.price / 100.0 << ","
              << r.quantity << ","
              << r.order_received_ns << ","
              << r.trade_executed_ns << ","
              << r.latency_ns << "\n";
        }
        std::cout << "Trades    → " << path
                  << " (" << records_.size() << " fills)\n";
    }

    // Latency summary
    void printLatencySummary() const {
        if (records_.empty()) {
            std::cout << "  No trades recorded.\n";
            return;
        }

        std::vector<uint64_t> latencies;
        latencies.reserve(records_.size());
        for (const auto& r : records_) latencies.push_back(r.latency_ns);
        std::sort(latencies.begin(), latencies.end());

        auto pct = [&](double p) {
            std::size_t idx = static_cast<std::size_t>(p * (latencies.size() - 1));
            return latencies[idx];
        };

        double mean = static_cast<double>(
            std::accumulate(latencies.begin(), latencies.end(), uint64_t(0))) / latencies.size();

        std::cout << "\n  Pipeline latency (order enqueued → trade executed):\n";
        std::cout << "    trades : " << records_.size() << "\n";
        std::cout << "    mean   : " << std::fixed << std::setprecision(0) << mean << " ns\n";
        std::cout << "    P50    : " << pct(0.50) << " ns\n";
        std::cout << "    P99    : " << pct(0.99) << " ns\n";
        std::cout << "    P999   : " << pct(0.999) << " ns\n";
        std::cout << "    max    : " << latencies.back() << " ns\n";
    }

    std::size_t tradeCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return records_.size();
    }

private:
    mutable std::mutex mutex_;
    std::vector<TradeRecord> records_;
};
