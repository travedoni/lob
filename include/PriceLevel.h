#pragma once
#include "Types.h"
#include <deque>
#include <cstddef>

// PriceLevel: Holds all resting orders at a specific price, 
// in time priority (FIFO).
class PriceLevel {
public:
    Price price;

    explicit PriceLevel(Price p) : price(p), totalQty_(0) {}

    // Add an order to the back
    void addOrder(Order* order) {
        orders_.push_back(order);
        totalQty_ += order->quantity;
    }

    // Remove a specific order by ID
    bool removeOrder(OrderId id) {
        for (auto it = orders_.begin(); it != orders_.end(); ++it) {
            if ((*it)->id == id) {
                totalQty_ -= (*it)->quantity;
                orders_.erase(it);
                return true;
            }
        }
        return false;
    }

    Order* getFront() {
        return orders_.empty() ? nullptr : orders_.front();
    }

    // Access front order
    void popFront() {
        if (!orders_.empty()) {
            totalQty_ -= orders_.front()->quantity;
            orders_.pop_front();
        }
    }

    // Reduce quantity of front order
    void reduceTop(Quantity qty) {
        if (orders_.empty()) {
            orders_.front()->quantity -= qty;
            totalQty_ -= qty;
        }
    }

    // Update total when an order's qty changes externally
    void adjustTotal(Quantity delta) { totalQty_ -= delta; }

    Quantity totalQuantity() const { return totalQty_; }
    bool empty() const { return orders_.empty(); }
    size_t orderCount() const { return orders_.size(); }

private:
    std::deque<Order*> orders_;
    Quantity totalQty_;
};
