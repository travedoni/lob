#include "MatchingEngine.h"
#include "BookPrinter.h"
#include <iostream>
#include <sstream>
#include <string>
#include <stdexcept>

// Helper
// Prices are input as decimals (e.g. 10.05) and stored as fixed-point int64
// (* 100). This avoids floating point in the matching engine.

Price parsePrice(const std::string& s) {
    double d = std::stod(s);
    return static_cast<Price>(d * 100.0 + 0.5); // round the nearest cent
}

void printHelp() {
    std::cout << R"(
Commands:
    buy  <price> <qty>              Submit a limit buy order
    sell <price> <qty>              Submit a limit sell order
    cancel <id>                     Cancel an order by ID
    modify <id> <new_price> <qty>   Modify order (price change = cancel+resubmit)
    book [levels]                   Print order book (default 5 levels)
    top                             Print best bid/ask, spread, mid
    help                            Show this menu
    quit                            Exit

Prices are in dollars (e.g. 99.50). Stored internally as fixed-point cents.
)";
}

int main() {
    MatchingEngine engine;
    std::string line;

    printHelp();

    while (true) {
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;

        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;

        try {
            if (cmd == "quit" || cmd == "q") {
                break;
            } else if (cmd == "help" || cmd == "h") {
                printHelp();
            } else if (cmd == "buy" || cmd == "sell") {
                std::string priceStr;
                Quantity qty;
                if (!(ss >> priceStr >> qty)) {
                    std::cout << "  Usage: " << cmd << " <price> <qty>\n";
                    continue;
                }
                Price p = parsePrice(priceStr);
                Side side = (cmd == "buy") ? Side::Buy : Side::Sell;

                auto trades = engine.submitOrder(side, p, qty);
                OrderId id = engine.lastOrderId();

                if (trades.empty()) {
                    std::cout << "  Order #" << id << " resting in book ("
                              << cmd << " $" << priceStr << " x" << qty << ")\n";
                } else {
                    BookPrinter::printTrades(trades);
                    if (engine.book().hasOrder(id)) {
                        std::cout << "  Order #" << id << " partially filled — remainder resting.\n";
                    } else {
                        std::cout << "  Order #" << id << " fully filled.\n";
                    }
                }
            } else if (cmd == "cancel") {
                OrderId id;
                if (!(ss >> id)) { 
                    std::cout << "Usage: cancel <id>\n"; 
                    continue; 
                }
                if (engine.cancelOrder(id)) {
                    std::cout << "  Order #" << id << " cancelled.\n";
                } else {
                    std::cout << "  Order #" << id << " not found.\n";
                }
            } else if (cmd == "modify") {
                OrderId id;
                std::string priceStr;
                Quantity qty;
                if (!(ss >> id >> priceStr >> qty)) {
                    std::cout << "Usage: modify <id> <new_price> <qty>\n";
                    continue;
                }
                Price p = parsePrice(priceStr);
                auto trades = engine.modifyOrder(id, p, qty);
                std::cout << "Order #" << id << " modified.\n";
                BookPrinter::printTrades(trades);
            } else if (cmd == "book") {
                int levels = 5;
                ss >> levels;
                BookPrinter::printBook(engine.book(), levels);
            } else if (cmd == "top") {
                BookPrinter::printTopOfBook(engine.book());
            } else {
                std::cout << "Unknown command. Type 'help'.\n";
            }
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n";
        }
    }

    return 0;
}

