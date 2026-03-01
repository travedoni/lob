#pragma once
#include "Types.h"
#include "ArrayOrderBook.h"
#include <vector>
#include <memory>
#include <chrono>
#include <stdexcept>

// ─── ArrayMatchingEngine ──────────────────────────────────────────────────────
// Same matching logic as MatchingEngine but uses ArrayOrderBook instead of
// the std::map-based OrderBook. Used purely for benchmarking comparison.

class ArrayMatchingEngine {
public:
    ArrayMatchingEngine() : nextOrderId_(1) {}

    std::vector<Trade> submitOrder(Side side, Price price, Quantity qty) {
        auto order = std::make_unique<Order>(nextOrderId_++, side, price, qty, now());
        Order* raw = order.get();
        storage_.push_back(std::move(order));

        std::vector<Trade> trades = match(raw);
        if (raw->quantity > 0)
            book_.addOrder(raw);
        return trades;
    }

    bool cancelOrder(OrderId id) { return book_.cancelOrder(id); }

    const ArrayOrderBook& book()        const { return book_; }
    OrderId               lastOrderId() const { return nextOrderId_ - 1; }

private:
    std::vector<Trade> match(Order* taker) {
        std::vector<Trade> trades;

        if (taker->side == Side::Buy) {
            auto& asks = book_.asks();
            while (taker->quantity > 0 && !asks.empty()) {
                auto& level = asks.front();
                if (level.price > taker->price) break;
                fillLevel(taker, level, trades);
                if (level.empty()) book_.cleanLevel(Side::Sell, level.price);
            }
        } else {
            auto& bids = book_.bids();
            while (taker->quantity > 0 && !bids.empty()) {
                auto& level = bids.front();
                if (level.price < taker->price) break;
                fillLevel(taker, level, trades);
                if (level.empty()) book_.cleanLevel(Side::Buy, level.price);
            }
        }
        return trades;
    }

    void fillLevel(Order* taker, PriceLevel& level, std::vector<Trade>& trades) {
        while (taker->quantity > 0 && !level.empty()) {
            Order* maker     = level.getFront();
            Quantity fillQty = std::min(taker->quantity, maker->quantity);
            trades.emplace_back(maker->id, taker->id, maker->price, fillQty);
            taker->quantity -= fillQty;
            maker->quantity -= fillQty;
            level.adjustTotal(fillQty);
            if (maker->quantity == 0) {
                book_.removeFromIndex(maker->id);
                level.popFront();
            }
        }
    }

    static Timestamp now() {
        return static_cast<Timestamp>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    ArrayOrderBook                      book_;
    std::vector<std::unique_ptr<Order>> storage_;
    OrderId                             nextOrderId_;
};
