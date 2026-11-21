#pragma once

#include <cstddef>
#include <memory>
#include <string_view>

#include "taskq/engine_kind.hpp"
#include "taskq/task.hpp"

namespace taskq {

// Этап 9: интерфейс индекса по номеру принадлежит потребителю — хранилищу
// задач, — и лежит отдельно от реализаций. До этапа 9 он был в engine.hpp
// вместе с StdTaskIndex, TableTaskIndex и хеш-таблицами, и task_store.hpp,
// включая интерфейс, получал и все реализации (DIP, docs/audit.md,
// запись 2). Теперь task_store.hpp включает только этот заголовок, а
// реализации — в engines.hpp.
// Этап 8: индекс задач по номеру — за интерфейсом движка.
//
// Хранилище задач (BasicTaskStore) — это и есть ITaskStore постановки:
// постановка, выборка по приоритету, поиск по номеру, смена состояния,
// обход. Выборка по приоритету — деревья очередей (std::map по TaskKey,
// этап 7), и они не хеш-таблицы: у них нет второй реализации, которую
// стоило бы сравнивать. Поиск по номеру — единственная хеш-таблица
// хранилища, и именно её постановка велит заменить собственной. Поэтому за
// интерфейсом — она: всё хранилище работает с номерами задач только через
// ITaskIndex, и одни и те же тесты проходят на каждой реализации. Выбор —
// параметр запуска (--engine, TASKQ_ENGINE).
//
// Location — параметр шаблона: что записано для задачи, знает хранилище
// (очередь и позиция в её дереве или в индексе отложенных).
//
// Интерфейс виртуальный, а не шаблонный параметр хранилища: движок
// выбирается при запуске, а не при компиляции. Цена — косвенный вызов на
// каждый поиск; она измерена в docs/benchmarks.md.
template <class Location>
class ITaskIndex {
public:
    virtual ~ITaskIndex() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual Location* find(TaskId id) noexcept = 0;
    [[nodiscard]] virtual const Location* find(TaskId id) const noexcept = 0;
    // Новая запись. false — номер уже есть, индекс не изменён. Сильная
    // гарантия: при броске индекс прежний.
    virtual bool insert(TaskId id, Location location) = 0;
    virtual bool erase(TaskId id) noexcept = 0;
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;
    // Место под count записей без рехеширования.
    virtual void reserve(std::size_t count) = 0;

protected:
    ITaskIndex() = default;
    ITaskIndex(const ITaskIndex&) = default;
    ITaskIndex& operator=(const ITaskIndex&) = default;
};


// Фабрика — объявлена здесь, определена в engines.hpp. Хранилище зовёт её в
// удобных конструкторах, не видя ни одной реализации; для записи TaskStore
// она инстанцирована в src/engines.cpp, для тестовых типов — в тестах,
// которые включают engines.hpp.
template <class Location>
[[nodiscard]] std::unique_ptr<ITaskIndex<Location>> make_task_index(EngineKind kind);

}  // namespace taskq
