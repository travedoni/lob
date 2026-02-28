#pragma once
#include <cstdint>
#include <string>

// Basic types
using OrderId = uint64_t;
using Price = int64_t;
using Quantity = uint64_t;
using Timestamp = uint64_t;

enum class Side: uint8_t { Buy, Sell };

inline std::string sideToStr(Side s) { return s == Side::Buy ? "BUY" : "SELL"; }

// Order
struct Order {
    OrderId id;
    Side side;
    Price price;
    Quantity quantity; // remaining quantity
    Quantity originalQty;
    Timestamp timestamp;

    Order(OrderId id, Side side, Price price, Quantity qty, Timestamp ts)
        : id(id), side(side), price(price), 
          quantity(qty), originalQty(qty), timestamp(ts) {}
};

// Trade (fill record)
struct Trade {
    OrderId makerOrderId;
    OrderId takerOrderId;
    Price price;
    Quantity quantity;

    Trade(OrderId maker, OrderId taker, Price p, Quantity q)
        : makerOrderId(maker), takerOrderId(taker), price(p), quantity(q) {}
};
