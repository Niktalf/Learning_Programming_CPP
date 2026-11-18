// Этап 9: фабрика движков инстанцирована здесь, один раз, для записи
// хранилища Store. Хранилище видит только объявление (storage_engine.hpp);
// реализации движков и хеш-таблиц компилируются в этом файле и больше нигде
// в ядре. Правка хеш-таблицы пересобирает этот файл, а не все, кто включает
// store.hpp (docs/audit.md, звёздочка).

#include "kvs/engines.hpp"
#include "kvs/store.hpp"

namespace kvs {

template std::unique_ptr<IStorageEngine<StoreEntry<Value>>> make_engine<StoreEntry<Value>>(EngineKind kind);

}  // namespace kvs
