#pragma once
#include "Types.h"
#include "PriceLevel.h"
#include <map>
#include <unordered_map>
#include <optional>
#include <memory>
#include <functional>

// OrderBook
// Maintains the bid and ask sides of the book.
//
// Data structure choices:
//  Bids: std::map<Price, PriceLevel, std::greater<Price>>
//        -> iteration gives best bid first (descending)
//  Asks: std::map<Price, PriceLevel>
//        -> iteration gives best ask first (ascending)
//  Lookup: std::unordered_map<OrderId, Order*>
//          -> 0(1) cancel/modify by order ID

class OrderBook {
public:
    using BidMap = std::map<Price, PriceLevel, std::greater<Price>>;
    using AskMap = std::map<Price, PriceLevel>;

    // Add a resting order to the book
    void addOrder(Order* order) {
        Price p = order->price;
        if (order->side == Side::Buy) {
            auto [it, _] = bids_.emplace(p, PriceLevel(p));
            it->second.addOrder(order);
        } else {
            auto [it, _] = asks_.emplace(p, PriceLevel(p));
            it->second.addOrder(order);
        }
        index_[order->id] = order;
    }

    // Cancel an order by ID
    bool cancelOrder(OrderId id) {
        auto it = index_.find(id);
        if (it == index_.end()) return false;

        Order* order = it->second;
        Price  p     = order->price;

        if (order->side == Side::Buy) {
            auto bit = bids_.find(p);
            if (bit != bids_.end()) {
                bit->second.removeOrder(id);
                if (bit->second.empty()) bids_.erase(bit);
            }
        } else {
            auto ait = asks_.find(p);
            if (ait != asks_.end()) {
                ait->second.removeOrder(id);
                if (ait->second.empty()) asks_.erase(ait);
            }
        }

        index_.erase(it);
        return true;
    }

    // Modify order quantity (reduce only to keep time priority)
    // Full reprice = cancel + re-add (loses time priority, handled in engine)
    bool modifyQuantity(OrderId id, Quantity newQty) {
        auto it = index_.find(id);
        if (it == index_.end()) return false;

        Order* order = it->second;
        if (newQty >= order->quantity) return false; // only reduce supported here

        Quantity delta = order->quantity - newQty;
        order->quantity = newQty;

        // Adjust the PriceLevel's total
        Price p = order->price;
        if (order->side == Side::Buy) {
            auto bit = bids_.find(p);
            if (bit != bids_.end()) bit->second.adjustTotal(delta);
        } else {
            auto ait = asks_.find(p);
            if (ait != asks_.end()) ait->second.adjustTotal(delta);
        }

        return true;
    }

    // Remove an empty price level (called by matching engine after fills)
    void cleanLevel(Side side, Price price) {
        if (side == Side::Buy) {
            auto it = bids_.find(price);
            if (it != bids_.end() && it->second.empty()) bids_.erase(it);
        } else {
            auto it = asks_.find(price);
            if (it != asks_.end() && it->second.empty()) asks_.erase(it);
        }
    }

    // Remove order from index (after fill by matching engine)
    void removeFromIndex(OrderId id) { index_.erase(id); }

    // Top-of-book queries
    std::optional<Price> bestBid() const {
        if (bids_.empty()) return std::nullopt;
        return bids_.begin()->first;
    }

    std::optional<Price> bestAsk() const {
        if (asks_.empty()) return std::nullopt;
        return asks_.begin()->first;
    }

    std::optional<double> spread() const {
        auto bid = bestBid();
        auto ask = bestAsk();
        if (!bid || !ask) return std::nullopt;
        return static_cast<double>(*ask - *bid) / 100.0;
    }

    std::optional<double> midPrice() const {
        auto bid = bestBid();
        auto ask = bestAsk();
        if (!bid || !ask) return std::nullopt;
        return (*bid + *ask) / 200.0;
    }

    bool hasOrder(OrderId id) const { return index_.count(id) > 0; }
    Order* getOrder(OrderId id) {
        auto it = index_.find(id);
        return it != index_.end() ? it->second : nullptr;
    }

    BidMap& bids() { return bids_; }
    AskMap& asks() { return asks_; }
    const BidMap& bids() const { return bids_; }
    const AskMap& asks() const { return asks_; }

private:
    BidMap bids_;
    AskMap asks_;
    std::unordered_map<OrderId, Order*> index_;
};
