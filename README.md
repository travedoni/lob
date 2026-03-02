# Low-Latency Limit Order Book (C++)

A high-performance limit order book and matching engine built in C++17.

The project is structured in three phases, each building on the last:

| Phase | Focus | What's built |
|---|---|---|
| 1 | Correctness | Matching engine, order book, interactive CLI, test suite |
| 2 | Performance | Pool allocator, alternative data structures, latency benchmarking |
| 3 | Concurrency | Lock-free queue, producer/consumer threads, pipeline latency |

---

## Project Structure

```
lob/
├── CMakeLists.txt
├── include/
│   ├── Types.h                  # Fundamental types (Order, Trade, Side)         [Phase 1]
│   ├── PriceLevel.h             # FIFO queue at a single price                   [Phase 1]
│   ├── OrderBook.h              # std::map-based bid/ask book                    [Phase 1]
│   ├── MatchingEngine.h         # Baseline engine — heap allocation              [Phase 1]
│   ├── BookPrinter.h            # Pretty-print utility                           [Phase 1]
│   ├── PoolAllocator.h          # Fixed-size arena allocator                     [Phase 2]
│   ├── ArrayOrderBook.h         # Sorted vector alternative to std::map          [Phase 2]
│   ├── PoolMatchingEngine.h     # Pool-backed matching engine                    [Phase 2]
│   ├── ArrayMatchingEngine.h    # Array book matching engine                     [Phase 2]
│   ├── Benchmark.h              # Latency measurement + CSV export               [Phase 2]
│   ├── SPSCQueue.h              # Lock-free single producer/consumer ring buffer [Phase 3]
│   ├── OrderGateway.h           # Producer thread — feeds orders into queue      [Phase 3]
│   ├── TradeLogger.h            # Trade recording with pipeline latency tracking [Phase 3]
│   └── ThreadedEngine.h         # Full producer → queue → consumer pipeline      [Phase 3]
├── src/
│   ├── main.cpp                 # Interactive CLI                                [Phase 1]
│   ├── benchmark.cpp            # Phase 2 benchmark — 3 scenarios                [Phase 2]
│   └── threaded_benchmark.cpp   # Phase 3 benchmark — throughput + backpressure  [Phase 3]
└── tests/
    ├── test_lob.cpp             # 11 tests — matching engine correctness         [Phase 1]
    └── test_thread_lob.cpp      # 10 tests — SPSC queue + threaded engine        [Phase 3]
```

Every phase only adds new files. Nothing from Phase 1 is modified in Phase 2 or 3. The dependency chain is: `Types → PriceLevel → OrderBook → MatchingEngine → PoolMatchingEngine → ThreadedEngine`.

---

## Core Concepts

### What is a Limit Order Book?

A limit order book (LOB) is the core data structure of any electronic exchange. It maintains two sorted lists of resting orders:

- **Bids**: buyers stating the maximum price they will pay, sorted highest first
- **Asks**: sellers stating the minimum price they will accept, sorted lowest first

```
ASK  $100.50   80 shares   ← cheapest seller (best ask)
ASK  $100.25   30 shares
─────────────────────────  spread = $0.75  |  mid = $100.125
BID   $99.75  100 shares   ← highest buyer (best bid)
BID   $99.50   50 shares
```

The **spread** is the gap between best bid and best ask. The **mid-price** is their average. When the two sides cross (a buyer's price >= a seller's price), a trade executes.

### Matching Rules: Price-Time Priority

When a new order arrives, the engine matches it against the opposite side using two rules applied in order:

1. **Price priority**: a better-priced order is always matched first. A bid at $100.00 beats a bid at $99.50.
2. **Time priority**: if two orders are at the same price, the one placed earlier is matched first (FIFO). Cancelling and resubmitting at the same price loses your queue position.

### Fixed-Point Pricing

All prices are stored as `int64_t` in fixed-point cents (e.g. $99.50 → `9950`). Floating-point is never used in the matching engine — equality comparisons on floats in a price map are undefined behaviour waiting to happen.

---

## Phase 1 — Core Matching Engine

### Features

- Add limit buy/sell orders
- Cancel orders by ID in O(1)
- Modify orders (reduce qty at same price preserves time priority; price change = cancel + resubmit)
- Price-time priority matching with multi-level sweep
- Partial fills — unfilled remainder rests in the book
- Top-of-book queries: best bid, best ask, spread, mid-price

### Data Structures

| Structure | Purpose | Complexity |
|---|---|---|
| `std::map<Price, PriceLevel, std::greater>` | Bid side (descending) | O(log n) insert/erase |
| `std::map<Price, PriceLevel>` | Ask side (ascending) | O(log n) insert/erase |
| `std::unordered_map<OrderId, Order*>` | O(1) lookup by ID | O(1) average |
| `std::deque<Order*>` | FIFO queue per price level | O(1) push/pop |

`Order` objects are owned by `MatchingEngine` via `std::vector<unique_ptr<Order>>`. Raw pointers in the book and index are non-owning references — clean RAII ownership with no `shared_ptr` overhead in the hot path.

### Interactive CLI

```
lob> buy  99.50 100       → Order #1 resting in book
lob> sell 100.50 80       → Order #2 resting in book
lob> sell 99.00 120       → [FILL] maker=#1 taker=#3 price=$99.50 qty=100
lob> book                 → Pretty-prints bid/ask ladder
lob> top                  → Best bid, best ask, spread, mid-price
lob> cancel 2             → Order #2 cancelled
lob> modify 1 99.75 50    → Order repriced, triggers re-match if crossing
```

### Test Suite — 11 tests

| Test | What it verifies |
|---|---|
| Resting buy with no match | Order stays in book when no cross exists |
| Exact match | Full fill at matching price |
| Partial fill | Remainder rests after partial fill |
| Price priority | Best-priced order matched first |
| Time priority | Earlier order at same price matched first |
| Cancel | Order removed from book |
| Cancel not found | Returns false gracefully |
| Modify reduce qty | Quantity reduced, time priority preserved |
| Modify price | Repriced order triggers match |
| Spread and mid-price | Correct arithmetic |
| Multi-level sweep | Aggressive order sweeps multiple levels |

---

## Phase 2 — Performance Engineering

Phase 2 adds two alternative implementations and a benchmarking harness to measure and compare them. Nothing in Phase 1 is modified.

### PoolAllocator

`PoolAllocator<T, N>` is a fixed-size arena that allocates `T` objects from a single contiguous block allocated once at construction. Uses a free-list internally so alloc and free are both O(1) with zero `malloc` calls in the hot path.

```
Before pool:  submitOrder() → malloc() → potential cache miss → ~167ns P99
After pool:   submitOrder() → pop free-list → same arena      →  ~84ns P99
```

### ArrayOrderBook

`ArrayOrderBook` replaces `std::map` with `std::vector<PriceLevel>` kept sorted via binary search. The motivation is cache locality: `std::map` nodes are heap-scattered, causing pointer-chasing cache misses on book traversal.

| Operation | `std::map` | Sorted vector |
|---|---|---|
| Insert price level | O(log n) | O(n) shift |
| Remove price level | O(log n) | O(n) shift |
| Find by price | O(log n) | O(log n) bsearch |
| Cache behaviour | Poor — pointer chasing | Excellent — contiguous |

At typical book depths (10–50 active price levels) the O(n) shift is 10–50 element moves, all within a single cache line. Whether this beats the map is hardware-dependent, the benchmark tells you.

### Benchmark Results (Apple M-series, 500k orders)

```
┌─────────────────────────────────┬──────────┬──────────┬──────────┬──────────┬──────────────────┐
│ Benchmark                       │  Min ns  │  P50 ns  │  P99 ns  │ P999 ns  │   Throughput/s   │
├─────────────────────────────────┼──────────┼──────────┼──────────┼──────────┼──────────────────┤
│ Map   | insert-only             │       0  │      83  │     167  │    1750  │          11.67M  │
│ Pool  | insert-only             │       0  │      42  │      84  │    1209  │          18.48M  │
│ Array | insert-only             │       0  │      42  │     125  │    1375  │          15.63M  │
│ Map   | mixed                   │       0  │      84  │     459  │    1167  │           7.46M  │
│ Pool  | mixed                   │       0  │      83  │     416  │     625  │           8.75M  │
│ Array | mixed                   │       0  │     125  │     542  │     875  │           6.34M  │
│ Map   | cancel                  │       0  │      42  │      42  │     167  │          30.17M  │
│ Pool  | cancel                  │       0  │      42  │      42  │     167  │          29.49M  │
│ Array | cancel                  │       0  │      42  │      42  │     208  │          30.64M  │
└─────────────────────────────────┴──────────┴──────────┴──────────┴──────────┴──────────────────┘
```

**Key findings:**
- Pool is **2x faster than Map on insert P99** (84ns vs 167ns) — eliminating per-order `malloc` halves tail latency
- Array and Pool **tie on insert P50** (both 42ns) — at shallow book depth, cache locality matches the pool's allocation advantage
- Array is **slowest on mixed workload** — element shifting on match-driven removal hurts more than cache locality helps
- **Pool + Map is the optimal combination** — zero-malloc hot path from the pool, O(log n) removal without shifting from the map
- **Cancel is engine-independent** — all three engines share the same `unordered_map` index, so cancel is ~42ns regardless of book structure
- **Min = 0ns** throughout — Apple Silicon timer resolution is ~41ns; sub-tick operations register as zero. P50 is the correct metric

---

## Phase 3 — Concurrency

Phase 3 separates order ingestion from matching into two independent threads connected by a lock-free queue. Nothing in Phase 1 or 2 is modified.

### Architecture

```
OrderGateway (producer thread)       Matching Thread (consumer)
──────────────────────────────       ──────────────────────────
submit orders                        matcherLoop()
       │                                    │
       ▼                                    ▼
    SPSCQueue  ──── lock-free ────►   dequeue order
    (ring buffer)                          │
                                           ▼
                                    PoolMatchingEngine
                                           │
                                           ▼
                                    TradeLogger → CSV
```

### SPSCQueue

`SPSCQueue<T, N>` is a lock-free Single Producer / Single Consumer ring buffer. No mutex, no kernel calls, no blocking — synchronization is done entirely with `std::atomic`.

**Why SPSC?** The general multi-producer/multi-consumer problem requires complex CAS loops. SPSC sidesteps this: only one thread writes (no write-write race), only one reads (no read-read race). The only race is read vs write, resolved with two atomics.

**Cache-line padding** is critical. `head_` and `tail_` are written by different threads. Without padding they share a cache line — every write by one thread invalidates the other's cache line, causing constant misses even though they touch different variables. `alignas(64)` forces each onto its own 64-byte cache line, eliminating false sharing.

**Power-of-2 capacity** enables wrapping via bitmasking (`& (N-1)`) instead of modulo (`% N`). Division is expensive; AND is one cycle.

### Correct Synchronization

Getting concurrency right is harder than it looks. Three specific decisions matter:

`ordersProcessed_` is `std::atomic<uint64_t>` with `memory_order_release` on write and `memory_order_acquire` on read. This ensures all matching work (trades logged, state updated) is visible to the main thread before the count increments. Using `relaxed` here would be a subtle bug: the count could increment before the logger write is visible, causing tests to see zero trades despite the count reaching N.

The gateway thread is **fully joined** before `dropped()` is read in `runSynthetic`. Reading a non-atomic variable from another thread without joining first is a data race, undefined behaviour in C++.

`matcherRunning_` uses `memory_order_relaxed` in the hot loop, the queue's own atomics provide the necessary ordering for the actual data, so relaxed is correct and faster here.

### Benchmark Results (Linux, 200k orders)

```
Pipeline latency (order enqueued → trade executed):
  P50  :   285 ns
  P99  : 3,065 ns
  P999 : 10,705 ns
  Throughput: ~0.4M orders/sec end-to-end
```

P99 spike vs P50 is OS scheduling jitter — the kernel occasionally preempts the matching thread. On a real trading system this is suppressed with CPU pinning and a realtime kernel.

The **backpressure scenario** (16-slot queue) shows the system under sustained load: no orders dropped (gateway spins rather than discarding), P50 rises to ~420ns as orders wait longer in the queue.

### Test Suite — 10 tests

| Test | What it verifies |
|---|---|
| Empty on construction | Queue initialises correctly |
| Push and pop | Basic enqueue/dequeue |
| FIFO order | Items come out in insertion order |
| Full queue returns false | Non-blocking push |
| Empty queue returns nullopt | Non-blocking pop |
| Wrap-around | Ring buffer wraps correctly at boundary |
| Concurrent 1M items | No lost or duplicated items under real concurrency |
| Engine processes all orders | All submitted orders are matched |
| Engine generates trades on cross | Matching produces fills |
| Drop accounting | processed + dropped == total sent |

---

## Build & Run

### Requirements

- C++17 compiler (clang++ on macOS, g++ on Linux)
- CMake 3.15+ (optional)

### Direct compilation (macOS)

```bash
# Phase 1
clang++ -std=c++17 -g -Iinclude tests/test_lob.cpp -o test_lob
clang++ -std=c++17 -g -Iinclude src/main.cpp -o lob

# Phase 2 — always -O3 for accurate numbers
clang++ -std=c++17 -O3 -march=native -DNDEBUG -Iinclude src/benchmark.cpp -o benchmark

# Phase 3 — -pthread required
clang++ -std=c++17 -g -Iinclude -pthread tests/test_phase3.cpp -o test_phase3
clang++ -std=c++17 -O3 -march=native -DNDEBUG -Iinclude -pthread src/threaded_benchmark.cpp -o threaded_benchmark
```

### CMake

```bash
mkdir build && cd build
cmake ..
make
```

### Run everything

```bash
# Phase 1
./test_lob                   # 11 correctness tests
./lob                        # interactive CLI

# Phase 2
./benchmark                  # 100k orders, 3 scenarios
./benchmark 500000           # 500k for stable percentiles

# Phase 3
./test_thread_lob            # 10 tests (includes concurrent 1M item stress test)
./threaded_benchmark         # 200k orders
./threaded_benchmark 500000  # push it harder
```

### Output files

| Binary | Files written |
|---|---|
| `./benchmark` | `lob_benchmark_summary.csv`, `lob_benchmark_samples.csv` |
| `./threaded_benchmark` | `threaded_trades.csv` |

---

## Key Design Decisions

**Fixed-point prices**: floating-point equality in a price map is undefined behaviour. Every real matching engine uses integers. We store price × 100 as `int64_t`.

**Raw pointers in book, unique_ptr in storage**: `MatchingEngine` owns all `Order` objects through `storage_`. The book and index hold raw non-owning pointers. Separates ownership from access, avoids `shared_ptr` overhead in the hot path.

**`std::map` for Phase 1**: correct, sorted, easy to reason about. Phase 2 challenges it with real measurements. Starting with correctness and optimising with data is the right engineering process.

**Pool on heap**: `PoolMatchingEngine` is always heap-allocated. A 1M-slot `Order` arena is ~80MB; stack-allocating it overflows immediately.

**SPSC over MPMC**: the single producer / consumer constraint eliminates all write-write and read-read races, allowing a simple two-atomic implementation. MPMC requires CAS loops and is significantly harder to implement correctly.

**Spinning over sleeping**: `std::this_thread::yield()` keeps response time at ~100ns. A condition variable adds ~50µs OS wakeup latency per empty queue check. For a matching engine the CPU core is worth it.

