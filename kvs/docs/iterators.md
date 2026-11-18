# Этап 7: итераторы, emplace, operator[] и инвалидация

## Собственный итератор хранилища

`BasicStore::Iterator` — прямой (forward) итератор по ключам. Обёртка над
итератором `unordered_map`, которая отдаёт только ключ: значение и версия —
подробности хранилища.

```cpp
static_assert(std::forward_iterator<Store::Iterator>);
static_assert(std::ranges::forward_range<const Store>);
```

Что для этого понадобилось — и почему каждое:

| Член | Зачем |
|---|---|
| `iterator_category = forward_iterator_tag` | категория для `std::iterator_traits` и старых алгоритмов |
| `value_type`, `reference`, `pointer`, `difference_type` | типы, которые смотрят концепции и алгоритмы |
| конструктор по умолчанию | требование `forward_iterator`: «пустой» итератор |
| `operator*`, `operator->` | разыменование; ключ — `const`, его менять нельзя |
| `operator++()` и `operator++(int)` | постфиксный возвращает копию до сдвига — её можно разыменовать, это и отличает forward от input |
| `operator==` | `= default`; `!=` C++20 выводит сам |

Хранилище получило `begin()` и `end()`. Теперь работает range-based for,
алгоритмы (`std::count_if`, `std::find`, `std::ranges::max_element`) и
конвейеры ranges. Конвейер KEYS этапа 5 теперь идёт по самому хранилищу —
`*this | views::filter | views::take` вместо `data_ | views::keys | …`, — и
в нём не изменилось ничего, кроме источника. Тесты —
`tests/test_store_iterator.cpp`.

Итератор действителен, пока таблицу не трогают: у `unordered_map` вставка с
рехешированием делает недействительными все итераторы, удаление — итератор
удалённого.

## Звёздочка: input-итератор со стражем

`store.scan(pattern)` — ключи, подходящие под образец. Итератор
`ScanIterator`:

- **input, а не forward.** Однопроходный и некопируемый: копировать его
  незачем, а запрет делает однопроходность проверяемой. `static_assert` в
  тестах это фиксирует — `input_iterator` да, `forward_iterator` нет;
- **ленивый.** Следующий подходящий ключ ищется только при `++`: ничего не
  вычисляется заранее, и `scan(...) | views::take(1)` смотрит ровно до
  первого совпадения;
- **со стражем.** Конец — отдельный тип `ScanEnd`, а не второй итератор.
  «Конец» здесь — не позиция, а условие «подходящих ключей больше нет», и
  итератор проверяет его сам: `it == ScanEnd{}`. Диапазон —
  `std::ranges::subrange<ScanIterator, ScanEnd>`, не `common_range`.

Конвейеры этапа 5 — `filter`, `take`, `transform` — работают на нём без
изменений: тест `Scan.StageFivePipelinesWorkUnchanged`. Ranges нужна только
модель концепции, а не конкретный итератор.

Почему это важно дальше: на этапе 13 разбор потока байтов из сокета —
диапазон, конец которого заранее неизвестен. Страж выражает это прямо.

## emplace против push, try_emplace против insert

`tests/test_emplace.cpp` на типе `Traced`, который пишет в журнал каждый
вызов конструктора:

| Вызов | Что происходит |
|---|---|
| `v.push_back(Traced("x"))` | construct, move — временный объект и перенос в вектор |
| `v.emplace_back("x")` | construct — объект рождается на месте |
| `v.emplace_back(variable)` | construct (переменная), copy — emplace не убирает копию lvalue, её убирает только `std::move` |
| `m.insert({"k", Traced("new")})` при занятом ключе | значение построено, перенесено в пару и выброшено |
| `m.try_emplace("k", "new")` при занятом ключе | ничего — аргументы даже не тронуты |
| `m.try_emplace("k", "x")` для нового ключа | construct — один раз |
| `m["k"] = Traced("x")` | construct, default, move-assign — лишнее значение по умолчанию |

Ещё одна вещь, которую видно на перевыделении вектора: элементы переносятся
перемещением, только если оно `noexcept` (`std::move_if_noexcept`). У
`Traced` оно `noexcept` — копий нет. Без `noexcept` вектор копировал бы
каждый элемент при каждом росте.

### Что исправлено в коде

- **Чтение без копии.** GET, TYPE, LLEN, STRLEN и INCR шли через
  `Store::get`, который возвращает копию значения. Для списка это копия всех
  элементов ради одного числа LLEN. Появился `Store::lookup` — ссылка через
  `reference_wrapper` — и читающие команды перешли на него. Это самое
  дорогое лишнее конструирование в проекте.
- **Список на месте.** RPUSH до этапа 7 копировал список, дописывал элемент
  и записывал копию через `set` — O(n) на вставку. Теперь
  `Store::modify` меняет значение там, где оно лежит, с проверкой предела
  до изменения и пересчётом учёта после.
- **MSET.** `pairs.emplace_back(std::string(k), Value(v))` строил два
  временных объекта и перемещал их в пару; `emplace_back(k, v)` от двух
  `string_view` строит строку и значение прямо в элементе.

Сознательно оставлено: `ListValue::push_back(std::string item)` — приём по
значению с перемещением внутрь. Перемещение строки — копия трёх указателей,
а интерфейс проще, чем пара перегрузок для `const&` и `&&`.

## Ловушка operator[]

Поиск по рабочему коду:

```bash
grep -rn '\]\s*=' include src
grep -rn 'operator\[\]' include src
```

Ни одного `operator[]` на ассоциативном контейнере в `include/` и `src/`
нет — ни сейчас, ни раньше: `Store::set` с этапа 1 шёл через `find` и
`insert`, потом `insert_or_assign`, реестр команд — через `try_emplace` и
`find`, кэш запросов — через `find` и `insert_or_assign`. `operator[]`
остался в двух местах, и оба нарочно:

- `tools/insert_order.cpp` и `tests/test_insert_order.cpp` — звёздочка
  этапа 1, показ того, что делает `operator[]`;
- `tests/test_emplace.cpp`, `Emplace.SubscriptAssignDefaultConstructsFirst`
  — то же на этом этапе.

Почему это важно именно здесь: `const` метод хранилища с `operator[]` не
соберётся — у `operator[]` нет константной версии, — а неконстантный
незаметно создаст пустое значение для отсутствующего ключа. GET
несуществующего ключа превратился бы в запись пустой строки. `at` бросает
`std::out_of_range`, `find` отвечает «нет» — оба честнее.

## Домашнее задание: удаление внутри обхода

`tools/invalidation_demo.cpp` — обход с удалением в двух контейнерах
проекта, оба раза с одной и той же ошибкой: после `erase(it)` цикл делает
`++it`.

### unordered_map — ловит санитайзер адресов

```
$ ./kvs_invalidation_demo map        # сборка с -fsanitize=address
==2427==ERROR: AddressSanitizer: heap-use-after-free on address 0x7b931ede1760
READ of size 8 at 0x7b931ede1760 thread T0
    #0 ... _Hash_node<...>::_M_next() const  hashtable_policy.h:366
    #1 ... _Node_iterator_base<...>::_M_incr()  hashtable_policy.h:383
    #2 ... _Node_iterator<...>::operator++()  hashtable_policy.h:435
    #3 ... broken_map  tools/invalidation_demo.cpp:41
0x7b931ede1760 is located 0 bytes inside of 56-byte region
freed by thread T0 here:
    #0 ... operator delete(void*, unsigned long)
    ...
    #9 ... unordered_map<...>::erase(...)  unordered_map.h:817
    #10 ... broken_map  tools/invalidation_demo.cpp:43
```

Итератор `unordered_map` — указатель на узел. `erase` освободил узел, `++it`
читает из него указатель на следующий: чтение освобождённой памяти,
строка 41 — `++it`, строка 43 — `erase`.

### deque — санитайзер молчит

```
$ ./kvs_invalidation_demo deque      # та же сборка с -fsanitize=address
deque: 32 left
```

Санитайзер ничего не нашёл, и программа завершилась нормально. `erase` в
середине deque сдвигает элементы и делает недействительными все итераторы,
но память блоков на месте: обращение к ней — не обращение к освобождённому.
Неопределённое поведение есть, а признаков нет. Это худший вид ошибки.

Её ловит отладочный режим libstdc++, `-D_GLIBCXX_DEBUG`: каждый итератор
знает свой контейнер и проверяет, не стал ли он недействительным.

```
$ ./kvs_invalidation_debug deque
.../debug/safe_iterator.h:392:
In function:
    gnu_debug::_Safe_iterator<...>::operator++()
    [with _Iterator = std::_Deque_iterator<int, int&, int*>; ...]

Error: attempt to increment a singular iterator.

Objects involved in the operation:
    iterator "this" @ 0x7fff544cdf70 {
      type = std::_Deque_iterator<int, int&, int*> (mutable iterator);
      state = singular;
      references sequence with type 'std::debug::deque<int, std::allocator<int> >'
    }
```

Цель `kvs_invalidation_debug` собирается всегда, и тест ctest
`kvs_invalidation_detected` проверяет, что отладочный режим это сообщение
выдал. Вывод из обоих инструментов: санитайзер адресов видит память, а не
правила контейнера. Для итераторов нужен инструмент, который знает правила.

### Исправление

`tests/test_invalidation.cpp`: `erase` возвращает итератор на следующий
элемент, действительный; инкремент — только в ветке без удаления.

```cpp
for (auto it = table.begin(); it != table.end();) {
    if (it->second % 2 == 0) {
        it = table.erase(it);
    } else {
        ++it;
    }
}
```

Короче — `std::erase_if(c, pred)` из C++20; ручной цикл нужен, когда при
удалении надо сделать что-то ещё.

### Какие операции std::deque делают итераторы недействительными

Контейнер списка — `std::deque`, и для команд списка правила такие:

| Операция | Итераторы | Ссылки и указатели |
|---|---|---|
| `push_front`, `push_back` (LPUSH, RPUSH) | все недействительны | действительны |
| `pop_front`, `pop_back` (LPOP, RPOP) | только снятый элемент (и `end()` у `pop_back`) | только снятый |
| `insert`, `erase` в середине | все | все |
| `erase` в начале или в конце | как у `pop` | как у `pop` |

Первая строка — главное отличие от vector: у deque вставка в концы не
двигает элементы, только карту блоков. Тест
`Invalidation.DequeKeepsReferencesButNotIteratorsOnPushAtEnds` проверяет то,
что можно проверить без неопределённого поведения, — адреса элементов после
тысячи вставок в оба конца; недействительность итераторов проверяет
отладочный режим. Для KVS отсюда правило: `ListValue` наружу отдаёт
элементы по значению (`range`, `pop_*`) или ссылку на весь контейнер только
для чтения (`items()`), но не итераторы — их бы погубил первый LPUSH.
