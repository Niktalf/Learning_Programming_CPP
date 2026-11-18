#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "kvs/engine_kind.hpp"
#include "kvs/version.hpp"

namespace kvs {

// Этап 9: интерфейс движка принадлежит потребителю — хранилищу, — и лежит
// отдельно от реализаций. До этапа 9 он был в engine.hpp вместе с
// StdEngine, TableEngine и тремя хеш-таблицами, и store.hpp, включая
// интерфейс, получал и все реализации: потребитель зависел от того, что
// должен был только объявить (DIP, docs/audit.md, запись 2). Теперь
// store.hpp включает только этот заголовок, а реализации — в engines.hpp,
// и их видят точка сборки, тесты движков и замеры.

// Запись таблицы: значение, версия и момент последней записи — по
// монотонным часам (IClock, clock.hpp). Тип открыт, а не вложен в
// BasicStore: движок создаётся вне хранилища, и тому, кто его создаёт,
// нужно назвать тип записи.
template <class V>
struct StoreEntry {
    V value;
    RecordVersion version;
    std::chrono::steady_clock::time_point touched{};
};

// Этап 8: хранилище за интерфейсом движка.
//
// Движок — таблица «ключ → запись»: put, get (find), erase, contains, size и
// обход. Всё остальное — учёт объёма, версии, предел памяти, списки,
// диапазонный индекс — остаётся в BasicStore и одинаково для всех движков.
// Поэтому одни и те же тесты проходят на каждом: тесты знают только
// BasicStore, а движок выбирается при его создании.
//
// Обход — по позициям: корзина и номер в ней. Позиция — не итератор
// конкретного контейнера, а два числа, которые понимает любая хеш-таблица,
// и на них стоят и итератор хранилища этапа 7, и SCAN с курсором: курсор
// SCAN — номер корзины, как в Redis.
//
// Виртуальный интерфейс, а не шаблонный параметр: движок выбирается при
// запуске (--engine), а не при компиляции. Цена — косвенный вызов на каждую
// операцию; в docs/benchmarks.md видно, что она мала по сравнению с самой
// таблицей.
template <class Entry>
class IStorageEngine {
public:
    struct Slot {
        std::size_t bucket = 0;
        std::size_t index = 0;
        friend bool operator==(const Slot&, const Slot&) = default;
    };

    virtual ~IStorageEngine() = default;

    // Имя движка — для --engine и для отчётов.
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    // Глубокая копия: на ней стоит copy-and-swap у MSET.
    [[nodiscard]] virtual std::unique_ptr<IStorageEngine> clone() const = 0;

    // Указатель — наблюдатель, не владелец; пустой — ключа нет. Действителен
    // до следующей вставки: у открытой адресации вставка может перенести
    // все элементы.
    [[nodiscard]] virtual Entry* find(std::string_view key) noexcept = 0;
    [[nodiscard]] virtual const Entry* find(std::string_view key) const noexcept = 0;

    // Вставка или замена с сильной гарантией. true — ключ новый.
    virtual bool put(std::string key, Entry entry) = 0;

    virtual bool erase(std::string_view key) noexcept = 0;

    [[nodiscard]] virtual std::size_t size() const noexcept = 0;

    [[nodiscard]] bool contains(std::string_view key) const noexcept { return find(key) != nullptr; }

    // --- Обход по позициям -----------------------------------------------
    [[nodiscard]] virtual std::size_t bucket_count() const noexcept = 0;
    [[nodiscard]] virtual std::optional<Slot> first_from(std::size_t bucket) const noexcept = 0;
    [[nodiscard]] virtual std::optional<Slot> next(Slot slot) const noexcept = 0;
    [[nodiscard]] virtual const std::string& key_at(Slot slot) const noexcept = 0;
    [[nodiscard]] virtual const Entry& entry_at(Slot slot) const noexcept = 0;

protected:
    IStorageEngine() = default;
    IStorageEngine(const IStorageEngine&) = default;
    IStorageEngine& operator=(const IStorageEngine&) = default;
};


// Фабрика движков — объявлена здесь, определена в engines.hpp. Хранилище
// зовёт её в удобных конструкторах, не видя ни одной реализации; для записи
// StoreEntry<Value> она инстанцирована один раз в infrastructure/src/engines.cpp, для
// тестовых типов — в тестах, которые включают engines.hpp.
template <class Entry>
[[nodiscard]] std::unique_ptr<IStorageEngine<Entry>> make_engine(EngineKind kind);

}  // namespace kvs
