# Алгоритмы вместо циклов (этап 8, пункт 5)

Правило, по которому шёл проход: цикл заменяется алгоритмом, если у цикла
есть имя — «найти», «посчитать», «удалить такие», «все ли», «есть ли
хоть один», «свернуть», «преобразовать каждый». Имя алгоритма говорит, что
делается; цикл заставляет читателя восстановить это по телу. Если имени у
цикла нет — конечный автомат, разбор с откатом, действие с побочным
эффектом на каждом шаге, — цикл остаётся, и ниже сказано почему.

## Что заменено

| где | было | стало |
|---|---|---|
| `chat_store.hpp`, `members` | цикл с `lock` и `if` + `push_back` | `ranges::transform` (lock), `std::erase(alive, nullptr)`, `ranges::sort` с проекцией по нику |
| `chat_store.hpp`, `live_members` | (новое: число участников для `/rooms`) | `ranges::count_if` по `!expired()` |
| `chat_store.hpp`, `post_many`, повтор комнаты | `std::find(...) != end` | `ranges::contains` (C++23) |
| `chat_store.hpp`, `post_many`, копии | цикл со счётчиком | `ranges::generate_n` |
| `chat_store.hpp`, `post_many`, запас ёмкости | цикл | `ranges::for_each` |
| `chat_store.hpp`, `room_page` | — | `upper_bound` + `views::take` + `ranges::copy` |
| `chat_store.hpp`, `history_between` | — | `lower_bound`/`upper_bound` + `take` / `reverse` + `ranges::transform` |
| `chat_store.hpp`, `for_each_room` | цикл по `map` | `ranges::for_each` по индексу имён |
| `chat_store.hpp`, `purge` | (этап 7) | `std::erase_if` |
| `engine.hpp`, `parse_engine_kind` | — | `ranges::find` с проекцией `name_of` |
| `commands.cpp`, `is_reaction` | цикл с ранним `return false` | `ranges::all_of` |
| `commands.cpp`, `/msg`, проверки | цикл | `ranges::for_each` |
| `commands.cpp`, строки истории и `/rooms` | цикл с `push_back` | `ranges::transform` в `back_inserter` |
| `validate.cpp`, имя | цикл с ранним выходом | `ranges::all_of` |
| `validate.cpp`, управляющие символы | цикл с ранним выходом | `ranges::any_of` |
| `message_id.cpp`, байты автора | цикл с `push_back` | `ranges::transform` |
| `server.cpp`, цепочка контекста ошибки | цикл с `+=` | `ranges::fold_left` |
| `registry.hpp`, `lower` | цикл по символам | `ranges::transform` |
| `registry.hpp`, `names` | цикл с `push_back` | `ranges::to` от `views::keys` |
| `static_registry.hpp`, `distinct_verbs` | двойной цикл, O(n²) | `ranges::sort` + `ranges::adjacent_find`, по-прежнему `constexpr` |
| `hash.hpp`, FNV-1a и сумма кодов | — | `ranges::fold_left`, `constexpr` |

## erase-remove, `std::erase` и `std::erase_if`

Идиома erase-remove — для последовательных контейнеров:

```cpp
v.erase(std::remove(v.begin(), v.end(), nullptr), v.end());
```

`remove` ничего не удаляет: он сдвигает оставляемые элементы в начало и
возвращает новый конец, а в хвосте остаются элементы в перемещённом
состоянии. Удаляет их `erase`. Забыть его — частая ошибка: размер вектора
прежний, в хвосте мусор. Для `set` и `map` `remove` неприменим вовсе —
элементы константны и переставлять их нельзя, — и до C++20 удаление по
условию писали циклом `it = c.erase(it)`; его ошибочный вариант —
`tools/invalidation_demo.cpp` этапа 7.

C++20 закрыл оба случая: `std::erase(c, value)` и `std::erase_if(c, pred)`
перегружены для всех стандартных контейнеров, сами выбирают правильный
способ и возвращают число удалённых. В чате:

- `members` — `std::erase(alive, nullptr)`: вектор `shared_ptr`, полученный
  `lock` из состава комнаты, теряет истёкших участников — erase-remove на
  векторе одной строкой;
- `purge` — `std::erase_if(members, expired)`: тот же смысл на
  `std::set<weak_ptr>`, где erase-remove невозможен.

## `/rooms` с курсором на алгоритмах

```cpp
const auto first = after ? room_order_.upper_bound(*after) : room_order_.begin();
std::ranges::copy(std::ranges::subrange(first, room_order_.end()) | std::views::take(taken),
                  std::back_inserter(page.names));
```

и в команде — `transform` имени в строку «имя (число)», где число —
`count_if` живых участников. Курсор — имя последней комнаты страницы;
почему имя, а не номер, — docs/rbtree.md.

## Что осталось циклами и почему

- **Лексер** (`lexer.cpp`) — конечный автомат: пробелы, кавычки, хвост
  строки как один аргумент. Положение в строке меняется по-разному в
  зависимости от состояния; `views::split` кавычек не знает.
- **Разбор списка комнат** (`convert.cpp`) — поиск запятых с проверкой
  каждого имени и броском на первом плохом; `views::split` дал бы куски,
  но проверка с исключением внутри конвейера читалась бы хуже цикла.
  Поиск повтора внутри — уже `ranges::contains`.
- **Проверка UTF-8** (`validate.cpp`) — автомат: длина шага зависит от
  первого байта последовательности.
- **Цепочка вложенных исключений** (`errors.cpp`) — следующий элемент
  получается из предыдущего (`rethrow_if_nested`), диапазона заранее нет.
- **`history_where`** — материализация ленивого конвейера обычным циклом
  сознательно, с этапа 5: `ranges::to` для прямого диапазона проходит его
  дважды, и предикат вызывался бы почти вдвое чаще (комментарий в коде).
- **`post_many`, шаг 1** — поиск комнаты бросает исключение с именем
  комнаты; `for_each` с бросающей лямбдой читался бы не лучше.
- **`post_many`, узлы индекса и откат** — два параллельных вектора и
  откат ровно вставленных узлов в `catch`; `views::zip` здесь возможен, но
  индекс нужен для отката, и цикл по индексу честнее.
- **`commit`** — `noexcept`-фиксация по двум параллельным векторам.
- **Собственные таблицы** — обход цепочки и пробег по ячейкам: это и есть
  реализация поиска, на который опираются алгоритмы снаружи.
- **`message_id.cpp`, сдвиги по 8 бит** — тело зависит от номера шага, а не
  от элемента.
- **`main.cpp`** — разбор аргументов и цикл `getline`: «пока есть вход —
  действие с побочным эффектом».
- **`Shape` в `registry.hpp`** — `constexpr`-подсчёт по массиву флагов при
  компиляции; `find_if` по `views::iota` вышел бы длиннее и не понятнее.
