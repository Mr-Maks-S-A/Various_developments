#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <SPSC_EventQueue/spsc_event_queue.hpp>
#include <variant>
#include <vector>

// --- Пример простых событий ---
struct key_event { int key; };
struct mouse_event { float x, y; };
using engine_event = std::variant<key_event, mouse_event>;

// --- Простейший эмулятор Линейной Арены Памяти ---
class SimpleArena {
public:
    explicit SimpleArena(size_t bytes) : m_buffer(bytes) {}
    
    void* allocate(size_t bytes, size_t alignment) {
        size_t space = m_buffer.size() - m_offset;
        void* ptr = m_buffer.data() + m_offset;
        if (std::align(alignment, bytes, ptr, space)) {
            m_offset = m_buffer.size() - space + bytes;
            return ptr;
        }
        throw std::bad_alloc();
    }

    void deallocate(void*, size_t) noexcept {
        // Линейная арена не освобождает память поштучно
    }

private:
    std::vector<std::byte> m_buffer;
    size_t m_offset{0};
};

// --- STL-совместимый адаптер к Арене ---
template <typename T>
class ArenaAllocator {
public:
    using value_type = T;

    explicit ArenaAllocator(SimpleArena& arena) : m_arena(&arena) {}

    template <typename U>
    ArenaAllocator(const ArenaAllocator<U>& other) : m_arena(other.m_arena) {}

    T* allocate(size_t n) {
        return static_cast<T*>(m_arena->allocate(n * sizeof(T), alignof(T)));
    }

    void deallocate(T* p, size_t n) noexcept {
        m_arena->deallocate(p, n * sizeof(T));
    }

    SimpleArena* m_arena;
};

TEST_CASE("SPSC Queue with Custom Arena Allocator") {
    // 1. Предвыделяем 64 КБ из системы
    SimpleArena memory_arena(1024 * 64);
    ArenaAllocator<engine_event> alloc(memory_arena);

    // 2. Создаем SPSC-очередь на 1024 элемента, память под которую забирается из Арены
    fluxborn::spsc_event_queue<engine_event, ArenaAllocator<engine_event>> queue(1024, alloc);

    SUBCASE("Push and Pop Events") {
        CHECK(queue.empty());

        // In-place создание событий
        CHECK(queue.emplace(key_event{ .key = 32 }));
        CHECK(queue.emplace(mouse_event{ .x = 100.0f, .y = 200.0f }));

        engine_event evt;
        
        CHECK(queue.pop(evt));
        CHECK(std::holds_alternative<key_event>(evt));

        CHECK(queue.pop(evt));
        CHECK(std::holds_alternative<mouse_event>(evt));

        CHECK(queue.empty());
    }
}