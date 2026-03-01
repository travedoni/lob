#pragma once
#include "Types.h"
#include <vector>
#include <algorithm>
#include <numeric>
#include <chrono>
#include <string>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <functional>
#include <cmath>

// LatencyStats
struct LatencyStats {
    std::string label;
    double minNs;
    double maxNs;
    double meanNs;
    double p50Ns;
    double p99Ns;
    double p999Ns;
    double throughputPerSec;
    std::size_t sampleCount;

    // Raw samples kept for CSV export
    std::vector<double> samples;
};

// Benchmark
// Measures nanosecond latency of arbitrary operations via a callable.
// Records every individual sample so percentiles are exact (not estimated).
class Benchmark {
public:
    // Run a benchmark
    // fn() is called warmupRounds + measureRounds times.
    // Warmup rounds are discarded (they prime caches and branch predictors).
    static LatencyStats run(
        const std::string& label,
        std::size_t measureRounds,
        std::size_t warmupRounds,
        std::function<void()> fn)
    {
        std::vector<double> samples;
        samples.reserve(measureRounds);

        // Warmup
        for (std::size_t i = 0; i < warmupRounds; ++i) fn();

        // Measure
        for (std::size_t i = 0; i < measureRounds; ++i) {
            auto t0 = now();
            fn();
            auto t1 = now();
            samples.push_back(static_cast<double>(t1 - t0));
        }

        return computeStats(label, samples);
    }

    // Run a batch benchmark
    // fn(i) is called for each i, with individual timing per call.
    static LatencyStats runBatch(
        const std::string& label,
        std::size_t N,
        std::function<void(std::size_t)> fn,
        std::size_t warmup = 0)
    {
        std::vector<double> samples;
        samples.reserve(N);

        for (std::size_t i = 0; i < warmup; ++i) fn(i);

        for (std::size_t i = warmup; i < N + warmup; ++i) {
            auto t0 = now();
            fn(i);
            auto t1 = now();
            samples.push_back(static_cast<double>(t1 - t0));
        }

        return computeStats(label, samples);
    }

    // Print a results table to stdout
    static void printTable(const std::vector<LatencyStats>& results) {
        std::cout << "\n";
        std::cout << "┌─────────────────────────────────┬──────────┬──────────┬──────────┬──────────┬──────────┬──────────────────┐\n";
        std::cout << "│ Benchmark                       │  Min ns  │  P50 ns  │  P99 ns  │ P999 ns  │  Max ns  │   Throughput/s   │\n";
        std::cout << "├─────────────────────────────────┼──────────┼──────────┼──────────┼──────────┼──────────┼──────────────────┤\n";

        for (const auto& r : results) {
            std::cout << "│ " << std::left << std::setw(31) << r.label << " │"
                      << std::right << std::setw(8) << fmtNs(r.minNs) << " │"
                      << std::setw(8) << fmtNs(r.p50Ns) << " │"
                      << std::setw(8) << fmtNs(r.p99Ns) << " │"
                      << std::setw(8) << fmtNs(r.p999Ns) << " │"
                      << std::setw(8) << fmtNs(r.maxNs) << " │"
                      << std::setw(16) << fmtThroughput(r.throughputPerSec) << " │\n";
        }

        std::cout << "└─────────────────────────────────┴──────────┴──────────┴──────────┴──────────┴──────────┴──────────────────┘\n";
        std::cout << "  (ns = nanoseconds)\n\n";
    }

    // Export results to CSV
    // Two files:
    //   <prefix>_summary.csv, one row per benchmark, the percentile stats
    //   <prefix>_samples.csv, all raw latency samples (for histogram/CDF)
    static void exportCSV(
        const std::vector<LatencyStats>& results,
        const std::string& prefix = "benchmark")
    {
        // Summary CSV
        {
            std::string path = prefix + "_summary.csv";
            std::ofstream f(path);
            f << "benchmark,samples,min_ns,mean_ns,p50_ns,p99_ns,p999_ns,max_ns,throughput_per_sec\n";
            for (const auto& r : results) {
                f << std::fixed << std::setprecision(2)
                  << quote(r.label) << ","
                  << r.sampleCount << ","
                  << r.minNs << ","
                  << r.meanNs << ","
                  << r.p50Ns << ","
                  << r.p99Ns << ","
                  << r.p999Ns << ","
                  << r.maxNs << ","
                  << r.throughputPerSec << "\n";
            }
            std::cout << "Summary  → " << path << "\n";
        }

        // Raw samples CSV (one column per benchmark)
        {
            std::string path = prefix + "_samples.csv";
            std::ofstream f(path);

            // Header
            for (std::size_t i = 0; i < results.size(); ++i) {
                f << quote(results[i].label);
                if (i + 1 < results.size()) f << ",";
            }
            f << "\n";

            // Rows
            std::size_t maxRows = 0;
            for (const auto& r : results)
                maxRows = std::max(maxRows, r.samples.size());

            for (std::size_t row = 0; row < maxRows; ++row) {
                for (std::size_t col = 0; col < results.size(); ++col) {
                    if (row < results[col].samples.size())
                        f << std::fixed << std::setprecision(1) << results[col].samples[row];
                    if (col + 1 < results.size()) f << ",";
                }
                f << "\n";
            }
            std::cout << "Samples  → " << path << "\n";
        }
    }

private:
    static Timestamp now() {
        return static_cast<Timestamp>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    static LatencyStats computeStats(const std::string& label, std::vector<double>& samples) {
        std::sort(samples.begin(), samples.end());

        LatencyStats s;
        s.label = label;
        s.sampleCount = samples.size();
        s.samples = samples; // copy for CSV export

        s.minNs = samples.front();
        s.maxNs = samples.back();
        s.meanNs = std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
        s.p50Ns = percentile(samples, 0.50);
        s.p99Ns = percentile(samples, 0.99);
        s.p999Ns = percentile(samples, 0.999);

        // Throughput = 1 second / mean latency
        s.throughputPerSec = (s.meanNs > 0) ? 1e9 / s.meanNs : 0.0;

        return s;
    }

    static double percentile(const std::vector<double>& sorted, double p) {
        if (sorted.empty()) return 0.0;
        double idx = p * (sorted.size() - 1);
        std::size_t lo = static_cast<std::size_t>(idx);
        std::size_t hi = std::min(lo + 1, sorted.size() - 1);
        double frac = idx - lo;
        return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
    }

    static std::string fmtNs(double ns) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(0) << ns;
        return ss.str();
    }

    static std::string fmtThroughput(double t) {
        std::ostringstream ss;
        if (t >= 1e6) ss << std::fixed << std::setprecision(2) << t / 1e6 << "M";
        else if (t >= 1e3) ss << std::fixed << std::setprecision(2) << t / 1e3 << "K";
        else ss << std::fixed << std::setprecision(0) << t;
        return ss.str();
    }

    static std::string quote(const std::string& s) {
        return "\"" + s + "\"";
    }
};
