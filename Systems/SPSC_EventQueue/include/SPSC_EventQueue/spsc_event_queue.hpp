/**
 * @file spsc_event_queue.hpp
 * @brief Высокопроизводительная Lock-Free SPSC (Single-Producer Single-Consumer) очередь событий.
 * @details Реализация кольцевого буфера без блокировок, предназначенная для передачи
 *          данных между двумя параллельными потоками с минимальными задержками (latency).
 * @author fluxborn
 */

#pragma once

#include <memory>
#include <atomic>
#include <cstddef>
#include <cassert>
#include <utility>
#include <type_traits>

namespace fluxborn {

/**
 * @class spsc_event_queue
 * @brief Потокобезопасная Lock-Free очередь событий для сценария "Один Производитель — Один Потребитель".
 * 
 * @details Класс реализует кольцевой буфер фиксированного размера (степень двойки) с низким уровнем overhead.
 *          Основные оптимизации включают:
 *          - **Отсутствие CAS-операций:** Используются только атомарные `load` и `store` с `memory_order_release/acquire`.
 *          - **Защита от False Sharing:** Заголовки `m_head`, `m_tail` и их кэши выровнены по длине кэш-линии (64 байта).
 *          - **Кэширование индексов:** `m_head_cache` и `m_tail_cache` минимизируют межядерные обращения к L3-кэшу.
 *          - **Управление памятью:** Использование `cell` на базе `std::byte` позволяет конструировать объекты `T`
 *            in-place через placement new без зануления/дефолтной инициализации всей очереди.
 * 
 * @tparam T Тип сохраняемых элементов (событий).
 * @tparam Allocator Тип аллокатора памяти (по умолчанию std::allocator<T>).
 * 
 * @warning Класс **НЕ являeтся MPMC / MPSC / SPMC безопасным**. Вызов методов записи должен осуществляться 
 *          строго из одного потока (Producer), а вызов методов чтения — строго из другого (Consumer).
 */
template <typename T, typename Allocator = std::allocator<T>>
class spsc_event_queue {
public:
    using value_type       = T;
    using allocator_type  = Allocator;
    using size_type       = std::size_t;
    using reference       = value_type&;
    using const_reference = const value_type&;

private:
    /**
     * @brief Внутренняя ячейка кольцевого буфера для сырого хранения объекта T.
     * @details Предоставляет выровненную память для точечного управления временем жизни T (emplace/destroy).
     */
    struct cell {
        alignas(alignof(T)) std::byte storage[sizeof(T)];

        /**
         * @brief Получить указатель на экземпляр T.
         * @return T* Указатель на объект в сыром хранилище.
         */
        T* ptr() noexcept {
            return reinterpret_cast<T*>(storage);
        }

        /**
         * @brief Получить константный указатель на экземпляр T.
         * @return const T* Константный указатель на объект в сыром хранилище.
         */
        const T* ptr() const noexcept {
            return reinterpret_cast<const T*>(storage);
        }
    };

    using cell_allocator_type = typename std::allocator_traits<Allocator>::template rebind_alloc<cell>;
    using cell_traits         = std::allocator_traits<cell_allocator_type>;

    /// @brief Размер линии L1-кэша современных CPU (X86/ARM) в байтах.
    static constexpr size_type cache_line_size = 64;

    cell* m_buffer{nullptr};        ///< Динамический массив ячеек буфера.
    size_type m_capacity{0};        ///< Емкость очереди (всегда степень двойки).
    size_type m_buffer_mask{0};     ///< Маска для быстрой взятии остатка от деления (m_capacity - 1).

    /**
     * @brief Атомарный индекс записи (Producer).
     * @note Выровнен по размеру кэш-линии для предотвращения False Sharing с `m_tail`.
     */
    alignas(cache_line_size) std::atomic<size_type> m_head{0};

    /**
     * @brief Атомарный индекс чтения (Consumer).
     * @note Выровнен по размеру кэш-линии для предотвращения False Sharing с `m_head`.
     */
    alignas(cache_line_size) std::atomic<size_type> m_tail{0};

    /**
     * @brief Локальный кэш `m_head` для потока-потребителя (Consumer).
     * @details Избегает частых атомарных чтений `m_head` с синхронизацией шины CPU.
     */
    alignas(cache_line_size) size_type m_head_cache{0};

    /**
     * @brief Локальный кэш `m_tail` для потока-производителя (Producer).
     * @details Избегает частых атомарных чтений `m_tail` с синхронизацией шины CPU.
     */
    alignas(cache_line_size) size_type m_tail_cache{0};

    /// @brief Экземпляр аллокатора ячеек (без оверхеда по памяти, если аллокатор stateless).
    [[no_unique_address]] cell_allocator_type m_cell_allocator;

public:
    /**
     * @brief Конструирует очередь заданного объема.
     * 
     * @param capacity Максимальная емкость буфера. **Обязана быть степенью двойки** (например: 1024, 4096, 16384).
     * @param alloc Экземпляр кастомного аллокатора (опционально).
     * 
     * @pre `capacity` должна быть >= 2 и являться степенью двойки.
     */
    explicit spsc_event_queue(size_type capacity, const Allocator& alloc = Allocator())
        : m_capacity(capacity), m_cell_allocator(alloc) 
    {
        assert(capacity >= 2 && (capacity & (capacity - 1)) == 0 && "Capacity must be a power of 2!");
        m_buffer_mask = m_capacity - 1;

        // Предвыделение всей памяти единым монолитным блоком
        m_buffer = cell_traits::allocate(m_cell_allocator, m_capacity);
    }

    /**
     * @brief Уничтожает очередь и корректно освобождает все непрочитанные элементы.
     */
    ~spsc_event_queue() {
        if (m_buffer) {
            // Очищаем оставшиеся живые элементы в буфере
            value_type dummy;
            while (pop(dummy)) {}

            cell_traits::deallocate(m_cell_allocator, m_buffer, m_capacity);
        }
    }

    /// @brief Копирование очереди запрещено (SPSC — Lock-Free тип с упреждающим переносом владения).
    spsc_event_queue(const spsc_event_queue&) = delete;
    
    /// @brief Оператор копирования запрещен.
    spsc_event_queue& operator=(const spsc_event_queue&) = delete;

    /**
     * @brief Конструктор перемещения.
     * @param other Перемещаемый экземпляр очереди.
     */
    spsc_event_queue(spsc_event_queue&& other) noexcept
        : m_buffer(std::exchange(other.m_buffer, nullptr)),
          m_capacity(std::exchange(other.m_capacity, 0)),
          m_buffer_mask(std::exchange(other.m_buffer_mask, 0)),
          m_head(other.m_head.load(std::memory_order_relaxed)),
          m_tail(other.m_tail.load(std::memory_order_relaxed)),
          m_head_cache(other.m_head_cache),
          m_tail_cache(other.m_tail_cache),
          m_cell_allocator(std::move(other.m_cell_allocator)) {}

    // --- STL Interface ---

    /**
     * @brief Возвращает копию аллокатора очереди.
     * @return allocator_type Экземпляр исходного аллокатора элементов.
     */
    allocator_type get_allocator() const noexcept {
        return allocator_type(m_cell_allocator);
    }

    /**
     * @brief Возвращает максимальную емкость очереди.
     * @return size_type Число элементов, которое способна вместить очередь.
     */
    [[nodiscard]] size_type capacity() const noexcept {
        return m_capacity;
    }

    /**
     * @brief Проверяет, пуста ли очередь в текущий момент.
     * @return true Очередь пуста.
     * @return false В очереди есть как минимум один элемент.
     * @note Результат носит ориентировочный характер, если вызывается параллельно с работой Producer/Consumer.
     */
    [[nodiscard]] bool empty() const noexcept {
        return m_tail.load(std::memory_order_relaxed) == m_head.load(std::memory_order_relaxed);
    }

    // --- Producer API (Single Thread) ---

    /**
     * @brief Конструирует объект T непосредственно внутри ячейки буфера (In-Place / Zero-Copy).
     * 
     * @tparam Args Типы аргументов конструктора T.
     * @param args Аргументы для передачи в конструктор типа T.
     * @return true Элемент успешно сконструирован и добавлен в очередь.
     * @return false Очередь переполнена, элемент не был создан.
     * 
     * @note Вызывается **строго из потока-производителя (Producer)**.
     */
    template <typename... Args>
    bool emplace(Args&&... args) {
        const size_type current_head = m_head.load(std::memory_order_relaxed);

        // Быстрая проверка переполнения по локальному кэшу tail
        if (current_head - m_tail_cache >= m_capacity) {
            m_tail_cache = m_tail.load(std::memory_order_acquire);
            if (current_head - m_tail_cache >= m_capacity) {
                return false; // Буфер полон!
            }
        }

        cell* cell_ptr = &m_buffer[current_head & m_buffer_mask];

        // Placement new через allocator_traits
        using value_alloc_type = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;
        value_alloc_type val_alloc(m_cell_allocator);
        std::allocator_traits<value_alloc_type>::construct(val_alloc, cell_ptr->ptr(), std::forward<Args>(args)...);

        // Публикация элемента для Consumer с гарантией выталкивания памяти
        m_head.store(current_head + 1, std::memory_order_release);
        return true;
    }

    /**
     * @brief Копирует элемент в очередь.
     * @param value Ссылка на копируемый объект.
     * @return true Успешно помещено.
     * @return false Очередь переполнена.
     */
    bool push(const T& value) {
        return emplace(value);
    }

    /**
     * @brief Перемещает элемент в очередь.
     * @param value rvalue-ссылка на перемещаемый объект.
     * @return true Успешно помещено.
     * @return false Очередь переполнена.
     */
    bool push(T&& value) {
        return emplace(std::move(value));
    }

    // --- Consumer API (Single Thread) ---

    /**
     * @brief Извлекает элемент из головы очереди и перемещает его в `out_value`.
     * 
     * @param[out] out_value Ссылка на переменную, куда будет перемещено значение.
     * @return true Элемент успешно извлечен.
     * @return false Очередь пуста, извлечение не выполнено.
     * 
     * @note Вызывается **строго из потока-потребителя (Consumer)**.
     *       При успешном извлечении у элемента внутри очереди явно вызывается деструктор.
     */
    bool pop(T& out_value) {
        const size_type current_tail = m_tail.load(std::memory_order_relaxed);

        // Быстрая проверка пустоты по локальному кэшу head
        if (current_tail == m_head_cache) {
            m_head_cache = m_head.load(std::memory_order_acquire);
            if (current_tail == m_head_cache) {
                return false; // Очередь пуста!
            }
        }

        cell* cell_ptr = &m_buffer[current_tail & m_buffer_mask];

        // Перемещаем значение во внешний приемник
        out_value = std::move(*cell_ptr->ptr());

        // Явный вызов деструктора и явное завершение времени жизни T
        using value_alloc_type = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;
        value_alloc_type val_alloc(m_cell_allocator);
        std::allocator_traits<value_alloc_type>::destroy(val_alloc, cell_ptr->ptr());

        // Публикация свободного места для Producer
        m_tail.store(current_tail + 1, std::memory_order_release);
        return true;
    }
};

} // namespace fluxborn