#pragma once
#include "Types.h"
#include "OrderBook.h"
#include <vector>
#include <memory>
#include <chrono>
#include <stdexcept>

// MatchingEngine
// Owns all Order objects (via unique_ptr storage).
// Drives price-time priority matching.
//
// Matching rules:
//  A new BUY order matches against resting SELL orders (asks) if ask <= buy price.
//  A new SELL order matches against resting BUY orders (bids) if bid >= sell price.
//  Fills happen at the maker's resting price.
//  Any unfilled remainder rests in the book.

class MatchingEngine {
public: 
    MatchingEngine() : nextOrderId_(1) {}

    // Submit a new limit order
    // Returns list of trades generated (may be empty if no match).
    std::vector<Trade> submitOrder(Side side, Price price, Quantity qty) {
        auto order = std::make_unique<Order>(
            nextOrderId_++, side, price, qty, now()
        );
        Order* raw = order.get();
        storage_.push_back(std::move(order));

        std::vector<Trade> trades = match(raw);

        // If order has remaining quantity, rest it in the book
        if (raw->quantity > 0) {
            book_.addOrder(raw);
        }

        return trades;
    }

    // Cancel an existing order
    bool cancelOrder(OrderId id) {
        return book_.cancelOrder(id);
    }

    // Modify an order
    // Reducing qty: preserves time priority.
    // Changing price: cancel + resubmit (loses time priority).
    std::vector<Trade> modifyOrder(OrderId id, Price newPrice, Quantity newQty) {
        Order* order = book_.getOrder(id);
        if (!order) throw std::runtime_error("Order not found: " + std::to_string(id));

        if (newPrice == order->price) {
            // Same price level, just reduce qty
            if (newQty >= order->quantity)
                throw std::runtime_error("Modify can only redice qty at same price");
            book_.modifyQuantity(id, newQty);
            return {};
        }

        // Price change: cancel and resumbit 
        Side side = order->side;
        book_.cancelOrder(id);
        return submitOrder(side, newPrice, newQty);
    }

    // Book state queries
    const OrderBook& book() const { return book_; }
    OrderId lastOrderId() const { return nextOrderId_ - 1; }

private:
    std::vector<Trade> match(Order* taker) {
        std::vector<Trade> trades;

        if (taker->side == Side::Buy) {
            // Match against aks (ascending price) while ask <= taker price
            auto& asks = book_.asks();
            while (taker->quantity > 0 && !asks.empty()) {
                auto it = asks.begin(); //best ask (lowest)
                if (it->first > taker->price) break;

                PriceLevel& level = it->second;
                fillLevel(taker, level, trades);

                if (level.empty()) {
                    book_.cleanLevel(Side::Sell, it->first);
                }
            }
        } else {
            // Match agains bids (descending price) while bid >= taker price
            auto& bids = book_.bids();
            while (taker->quantity > 0 && !bids.empty()) {
                auto it = bids.begin(); // best bid (highest)
                if (it->first < taker->price) break;

                PriceLevel& level = it->second;
                fillLevel(taker, level, trades);

                if (level.empty()) {
                    book_.cleanLevel(Side::Buy, it->first);
                }
            }
        }

        return trades;
    }

    // Fill as much as possible at a single price level
    void fillLevel(Order* taker, PriceLevel& level, std::vector<Trade>& trades) {
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
            }
        }
    }

    // High-resolution timestamp
    static Timestamp now() {
        return static_cast<Timestamp>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );
    }

    OrderBook book_;
    std::vector<std::unique_ptr<Order>> storage_;
    OrderId nextOrderId_;
};
