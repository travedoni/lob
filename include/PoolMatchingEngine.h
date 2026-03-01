#pragma once
#include "Types.h"
#include "OrderBook.h"
#include "PoolAllocator.h"
#include <vector>
#include <chrono>
#include <stdexcept>

// PoolMatchingEngine
// Identical matching logic to MatchingEnginge, but uses PoolAllocator<Order>
// instead of std::vector<unique_ptr<Order>> for Order storage.
//
// This means:
//  - No malloc/free per order in the hot path
//  - All Order objects live in one contiguous arena, fewer cache misses
//  - The pool is pre-allocated at construction time
//
// Template parameter N controls pool capacity (default 1M orders).
// If the pool is exhausted an exception is thown.

template<std::size_t PoolSize = 1'000'000>
class PoolMatchingEngine {
public:
    PoolMatchingEngine() : nextOrderId_(1) {}

    std::vector<Trade> submitOrder(Side side, Price price, Quantity qty) {
        Order* order = pool_.alloc(nextOrderId_++, side, price, qty, now());
        std::vector<Trade> trades = match(order);
        if (order->quantity > 0) {
            book_.addOrder(order);
        }
        return trades;
    }

    bool cancelOrder(OrderId id) {
        Order* o = book_.getOrder(id);
        if (!o) return false;
        book_.cancelOrder(id);
        pool_.free(o);
        return true;
    }

    std::vector<Trade> modifyOrder(OrderId id, Price newPrice, Quantity newQty) {
        Order* order = book_.getOrder(id);
        if (!order) throw std::runtime_error("Order not found");

        if (newPrice == order->price) {
            if (newQty >= order->quantity) {
                throw std::runtime_error("Modify can only reduce qty at same price");
            }
            book_.modifyQuantity(id, newQty);
            return {};
        }

        Side side = order->side;
        book_.cancelOrder(id);
        pool_.free(order);
        return submitOrder(side, newPrice, newQty);
    }

    const OrderBook& book() const { return book_; }
    OrderId lastOrderId() const { return nextOrderId_ - 1; }
    std::size_t poolUsed() const { return pool_.allocated(); }

private:
    std::vector<Trade> match(Order* taker) {
        std::vector<Trade> trades;

        if (taker->side == Side::Buy) {
            auto& asks = book_.asks();
            while (taker->quantity > 0 && !asks.empty()) {
                auto it = asks.begin();
                if (it->first > taker->price) break;
                fillLevel(taker, it->second, trades);
                if (it->second.empty()) book_.cleanLevel(Side::Sell, it->first);
            }
        } else {
            auto& bids = book_.bids();
            while (taker->quantity > 0 && !bids.empty()) {
                auto it = bids.begin();
                if (it->first < taker->price) break;
                fillLevel(taker, it->second, trades);
                if (it->second.empty()) book_.cleanLevel(Side::Buy, it->first);
            }
        }
        return trades;
    }

    void fillLevel(Order* taker, PriceLevel& level, std::vector<Trade> trades) {
        while (taker->quantity > 0 && !level.empty()) {
            Order* maker = level.getFront();
            Quantity fillQty = std::min(taker->quantity, maker->quantity);

            trades.emplace_back(maker->id, taker->id, maker->price, fillQty);
            taker->quantity -= fillQty;
            maker->quantity -= fillQty;
            level.adjustTotal(fillQty);

            if (maker->quantity == 0) {
                book_.removeFromIndex(maker->id);
                level.popFront();
                pool_.free(maker); // return the pool, not heap-free
            }
        }
    }
    
    static Timestamp now() {
        return static_cast<Timestamp>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    OrderBook book_;
    PoolAllocator<Order, PoolSize> pool_;
    OrderId nextOrderId_;
};
