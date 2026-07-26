#include <benchmark/benchmark.h>
#include <thread>
#include <atomic>
#include <variant>
#include <cstdint>

#include <SPSC_EventQueue/spsc_event_queue.hpp>

// Имитация реального события игрового движка
struct PositionEvent { float x, y, z; uint32_t entity_id; };
struct KeyEvent     { int key; int scancode; int action; };
using EngineEvent = std::variant<PositionEvent, KeyEvent>;

// ==============================================================================
// 1. Throughput Benchmark: Прокачка миллиона событий между 2 потоками
// ==============================================================================
static void BM_SPSC_Throughput(benchmark::State& state) {
    const size_t queue_capacity = static_cast<size_t>(state.range(0));
    const uint64_t total_operations = 1'000'000;

    for (auto _ : state) {
        state.PauseTiming(); // Выключаем таймер на время аллокации
        
        fluxborn::spsc_event_queue<EngineEvent> queue(queue_capacity);
        std::atomic<bool> start_flag{false};
        std::atomic<uint64_t> consumed_count{0};

        // --- Consumer Thread ---
        std::thread consumer([&]() {
            while (!start_flag.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
            }

            EngineEvent event;
            uint64_t local_consumed = 0;
            while (local_consumed < total_operations) {
                if (queue.pop(event)) {
                    benchmark::DoNotOptimize(event); // Защита от оптимизации компилятора
                    local_consumed++;
                } else {
                    std::this_thread::yield(); // Пауза если очередь пуста
                }
            }
            consumed_count.store(local_consumed, std::memory_order_relaxed);
        });

        state.ResumeTiming(); // Включаем таймер!

        // Сигнал к старту для Consumer
        start_flag.store(true, std::memory_order_release);

        // --- Producer (Main Thread) ---
        uint64_t produced = 0;
        EngineEvent test_event = PositionEvent{ 1.0f, 2.0f, 3.0f, 42 };

        while (produced < total_operations) {
            if (queue.push(test_event)) {
                produced++;
            } else {
                std::this_thread::yield(); // Пауза если очередь полна
            }
        }

        consumer.join();
    }

    // Метрики для отчета: сколько операций обработано в секунду
    state.SetItemsProcessed(state.iterations() * total_operations);
    state.SetBytesProcessed(state.iterations() * total_operations * sizeof(EngineEvent));
}

// Регистрируем тест с разной емкостью буфера (1024, 4096, 16384 элементов)
BENCHMARK(BM_SPSC_Throughput)
    ->RangeMultiplier(4)
    ->Range(1024, 16384)
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();

// ==============================================================================
// 2. Single-Threaded Latency Benchmark: Замер чистого overhead на emplace/pop
// ==============================================================================
static void BM_SPSC_SingleThread_PushPop(benchmark::State& state) {
    fluxborn::spsc_event_queue<EngineEvent> queue(1024);
    EngineEvent push_event = PositionEvent{ 10.0f, 20.0f, 30.0f, 100 };
    EngineEvent pop_event;

    for (auto _ : state) {
        queue.push(push_event);
        queue.pop(pop_event);
        benchmark::DoNotOptimize(pop_event);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SPSC_SingleThread_PushPop)->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();