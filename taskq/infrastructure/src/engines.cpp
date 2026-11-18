// Этап 9: фабрика индекса по номеру инстанцирована здесь, один раз, для
// записи TaskStore. Хранилище видит только объявление (task_index.hpp);
// реализации и хеш-таблицы компилируются в этом файле и больше нигде в ядре.

#include "taskq/engines.hpp"
#include "taskq/task_store.hpp"

namespace taskq {

template std::unique_ptr<ITaskIndex<TaskStore::IndexEntry>> make_task_index<TaskStore::IndexEntry>(EngineKind kind);

}  // namespace taskq
