// Этап 9: фабрика данных чата инстанцирована здесь, один раз, для комнаты
// ChatStore. Хранилище видит только объявление (chat_data.hpp); реализации
// и хеш-таблицы компилируются в этом файле и больше нигде в ядре.

#include "chat/chat_store.hpp"
#include "chat/engines.hpp"

namespace chat {

template std::unique_ptr<IChatStore<ChatStore::Room>> make_chat_store<ChatStore::Room>(EngineKind kind);

}  // namespace chat
