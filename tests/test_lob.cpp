#include "../include/MatchingEngine.h"
#include <cassert>
#include <iostream>

static int passed = 0;
static int failed = 0;

#define TEST(name) std::cout << "  " << name << " ... "
#define PASS() do { std::cout << "\033[32mPASS\033[0m\n"; ++passed; } while(0)
#define FAIL(msg) do { std::cout << "\033[31mFAIL: " << msg << "\033[0m\n"; ++failed; } while(0)
#define ASSERT(cond, msg) do { if(cond) PASS(); else FAIL(msg); } while(0)

// Tests

void testNoMatchResting() {
    TEST("Resting buy with no matching ask");
    MatchingEngine e;
    auto trades = e.submitOrder(Side::Buy, 10000, 100);
    ASSERT(trades.empty() && e.book().bestBid().has_value(), "should rest in book");
}

void testExactMatch() {
    TEST("Exact price match buy/sell");
    MatchingEngine e;
    e.submitOrder(Side::Buy, 10000, 100);   // rest a buy
    auto trades = e.submitOrder(Side::Sell, 10000, 100); // should match
    ASSERT(trades.size() == 1 && trades[0].quantity == 100, "should produce 1 full fill");
}

void testPartialFill() {
    TEST("Partial fill — remainder rests");
    MatchingEngine e;
    e.submitOrder(Side::Buy, 10000, 50);   // rest 50 buy
    auto trades = e.submitOrder(Side::Sell, 10000, 100); // sell 100, fills 50, 50 left
    OrderId sellId = e.lastOrderId();
    ASSERT(trades.size() == 1 && trades[0].quantity == 50 && e.book().hasOrder(sellId),
           "50 filled, 50 should rest as sell");
}

void testPricePriority() {
    TEST("Price priority — best bid matched first");
    MatchingEngine e;
    e.submitOrder(Side::Buy, 9900, 100); // worse bid
    e.submitOrder(Side::Buy, 10000, 100); // better bid
    auto trades = e.submitOrder(Side::Sell, 9800, 100); // should match 10000 first
    ASSERT(!trades.empty() && trades[0].price == 10000, "best bid (10000) matched first");
}

void testTimePriority() {
    TEST("Time priority — earlier order at same price matched first");
    MatchingEngine e;
    e.submitOrder(Side::Buy, 10000, 50);
    OrderId firstId = e.lastOrderId();
    e.submitOrder(Side::Buy, 10000, 50);
    OrderId secondId = e.lastOrderId();
    auto trades = e.submitOrder(Side::Sell, 10000, 50);
    // First maker should be firstId (earlier)
    ASSERT(!trades.empty() && trades[0].makerOrderId == firstId,
           "first order placed should be matched first");
    (void)secondId;
}

void testCancel() {
    TEST("Cancel removes order from book");
    MatchingEngine e;
    e.submitOrder(Side::Buy, 10000, 100);
    OrderId id = e.lastOrderId();
    bool ok = e.cancelOrder(id);
    ASSERT(ok && !e.book().bestBid().has_value(), "book should be empty after cancel");
}

void testCancelNotFound() {
    TEST("Cancel non-existent order returns false");
    MatchingEngine e;
    ASSERT(!e.cancelOrder(9999), "should return false");
}

void testModifyReduceQty() {
    TEST("Modify reduce qty at same price");
    MatchingEngine e;
    e.submitOrder(Side::Buy, 10000, 100);
    OrderId id = e.lastOrderId();
    auto trades = e.modifyOrder(id, 10000, 50);
    ASSERT(trades.empty() && e.book().hasOrder(id), "order still resting with new qty");
}

void testModifyPrice() {
    TEST("Modify price — cancel + resubmit, triggers match");
    MatchingEngine e;
    e.submitOrder(Side::Sell, 10100, 100); // resting ask
    e.submitOrder(Side::Buy, 9900, 100);   // resting buy below ask
    OrderId buyId = e.lastOrderId();
    // Raise buy price to cross the ask → should match
    auto trades = e.modifyOrder(buyId, 10100, 100);
    ASSERT(!trades.empty(), "price modification should trigger match");
}

void testSpreadMid() {
    TEST("Spread and mid-price calculation");
    MatchingEngine e;
    e.submitOrder(Side::Buy,  9950, 10);  // $99.50
    e.submitOrder(Side::Sell, 10050, 10); // $100.50
    auto spread = e.book().spread();
    auto mid    = e.book().midPrice();
    ASSERT(spread.has_value() && std::abs(*spread - 1.00) < 0.001
           && std::abs(*mid - 100.00) < 0.001,
           "spread should be $1.00, mid $100.00");
}

void testMultiLevelSweep() {
    TEST("Aggressive order sweeps multiple price levels");
    MatchingEngine e;
    e.submitOrder(Side::Sell, 10000, 50);
    e.submitOrder(Side::Sell, 10100, 50);
    e.submitOrder(Side::Sell, 10200, 50);
    auto trades = e.submitOrder(Side::Buy, 10200, 150); // sweeps all 3 levels
    ASSERT(trades.size() == 3, "should produce 3 fills across 3 levels");
}

// Runner
int main() {
    std::cout << "\n════════════════════════════════════\n";
    std::cout <<   "  Limit Order Book — Test Suite\n";
    std::cout <<   "════════════════════════════════════\n\n";

    testNoMatchResting();
    testExactMatch();
    testPartialFill();
    testPricePriority();
    testTimePriority();
    testCancel();
    testCancelNotFound();
    testModifyReduceQty();
    testModifyPrice();
    testSpreadMid();
    testMultiLevelSweep();

    std::cout << "\n────────────────────────────────────\n";
    std::cout << "  Results: " << passed << " passed, " << failed << " failed\n";
    std::cout << "────────────────────────────────────\n\n";

    return failed > 0 ? 1 : 0;
}
