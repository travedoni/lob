#pragma once
#include "Types.h"
#include "OrderBook.h"
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

// BookPrinter
// Pretty-prints the order book and trade feed to stdout.

namespace BookPrinter {

    inline double toDecimal(Price p) { return p / 100.0; }

    inline void printBook(const OrderBook& book, int levels = 5) {
        const auto& bids = book.bids();
        const auto& asks = book.asks();

        std::cout << "\n╔══════════════════════════════════════════╗\n";
        std::cout <<   "║            LIMIT ORDER BOOK              ║\n";
        std::cout <<   "╠══════════════════════╦═══════════════════╣\n";
        std::cout <<   "║   Price       Qty    ║  Side             ║\n";
        std::cout <<   "╠══════════════════════╬═══════════════════╣\n";

        // Print asks in reverse (highest first visually, but best ask at center)
        std::vector<std::pair<Price, Quantity>> askLevels;
        int count = 0;
        for (auto it = asks.rbegin(); it != asks.rend() && count < levels; ++it, ++count) {
            askLevels.push_back({it->first, it->second.totalQuantity()});
        }
        for (auto& [p, q] : askLevels) {
            std::cout << "║  \033[31m" << std::setw(8) << std::fixed << std::setprecision(2)
                      << toDecimal(p) << "   " << std::setw(6) << q
                      << "\033[0m    ║  ASK               ║\n";
        }

        // Spread line
        auto spread = book.spread();
        auto mid = book.midPrice();
        if (spread) {
            std::cout << "╠══════════════════════╬═══════════════════╣\n";
            std::cout << "║  spread: $" << std::setw(6) << std::fixed << std::setprecision(2)
                      << *spread << "          ║  mid: $" << std::setw(7) << std::setprecision(2)
                      << *mid << "     ║\n";
            std::cout << "╠══════════════════════╬═══════════════════╣\n";
        } else {
            std::cout << "╠══════════════════════╬═══════════════════╣\n";
        }

        // Print bids (highest first)
        count = 0;
        for (auto& [p, level] : bids) {
            if (count++ >= levels) break;
            std::cout << "║  \033[32m" << std::setw(8) << std::fixed << std::setprecision(2)
                      << toDecimal(p) << "   " << std::setw(6) << level.totalQuantity()
                      << "\033[0m    ║  BID               ║\n";
        }

        std::cout << "╚══════════════════════╩═══════════════════╝\n";
    }

    inline void printTrades(const std::vector<Trade>& trades) {
        if (trades.empty()) return;
        std::cout << "\nTrades executed:\n";
        for (const auto& t : trades) {
            std::cout << "     [FILL] maker=#" << t.makerOrderId
                      << " taker=#" << t.takerOrderId
                      << "  price=$" << std::fixed << std::setprecision(2) << toDecimal(t.price)
                      << "  qty=" << t.quantity << "\n";
        }
    }

    inline void printTopOfBook(const OrderBook& book) {
        auto bid = book.bestBid();
        auto ask = book.bestAsk();
        std::cout << "  Top-of-book → ";
        if (bid) std::cout << "BID $" << std::fixed << std::setprecision(2) << toDecimal(*bid);
        else     std::cout << "BID [empty]";
        std::cout << "  |  ";
        if (ask) std::cout << "ASK $" << std::fixed << std::setprecision(2) << toDecimal(*ask);
        else     std::cout << "ASK [empty]";
        if (bid && ask) {
            std::cout << "  |  spread $" << std::setprecision(2) << *book.spread()
                      << "  mid $" << *book.midPrice();
        }
        std::cout << "\n";
    }

}
