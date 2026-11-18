# Этап 7: итератор очереди, emplace, operator[] и инвалидация

## Приоритетная выборка

POLL отдаёт задачу с наибольшим приоритетом, а при равенстве —
поставленную раньше. Ключ — `TaskKey` этапа 5 с написанным руками `<=>`, у
которого операнды приоритета переставлены: «меньше» значит «раньше выдать».
Очередь — `std::map<TaskKey, Task>`, и задачи лежат в нём ровно в порядке
выдачи: вершина — `begin()`. Почему дерево, а не `priority_queue`, — в
`docs/benchmarks.md`.

Тесты: `PriorityQueue.PollTakesHighestPriorityThenEarliest` — шесть задач со
смешанными и повторяющимися приоритетами, POLL их все, и порядок проверяется
целиком; `PriorityQueue.ReleasedTaskGoesBehindItsPeers` — возвращённая аренда
встаёт за задачами того же приоритета, но перед задачами ниже.

## Собственный итератор очереди

`TaskStore::QueueIterator` — прямой итератор по задачам очереди в порядке
выдачи. `store.view(queue)` возвращает `QueueView` с `begin`, `end` и `size`.

```cpp
static_assert(std::forward_iterator<QueueIterator>);
static_assert(std::ranges::forward_range<TaskStore::QueueView>);
static_assert(!std::is_assignable_v<std::iter_reference_t<QueueIterator>, Task>);
```

Обход не меняет очередь — по построению: итератор отдаёт `const Task&`,
`QueueView` не даёт ни одной изменяющей операции, а третий `static_assert`
проверяет, что через итератор задачу не перезаписать. POLL забирает, обход
смотрит; тест `QueueIterator.WalkingDoesNotChangeTheQueue` обходит очередь
трижды и проверяет, что она не изменилась, а затем показывает, что первым
POLL забирает ровно то, что обход показал первым.

Итератор — обёртка над итератором дерева, которая отдаёт задачу, а не пару
с ключом: ключ — подробность хранилища, и он и так есть у задачи.

С итератором очередь работает в range-based for, в классических алгоритмах
(`std::distance`, `std::count_if`, `std::find_if`) и в ranges
(`ranges::is_sorted` по приоритету по убыванию — проверка самого порядка,
`ranges::max_element`). Конвейер LIST этапа 5 теперь идёт по нему же, и
задачи в LIST приходят в порядке выдачи, а не постановки — LIST показывает
очередь такой, какой её увидит POLL. Код конвейера не изменился.

## Звёздочка: страж

`view.until_end()` — те же задачи, но конец — отдельный тип
`TaskStore::QueueEnd`. Итератор помнит конец своей очереди и сравнивается со
стражем сам. Диапазон — `subrange<QueueIterator, QueueEnd>`, не
`common_range`: range-based for и алгоритмы ranges его принимают,
классические алгоритмы — нет, поэтому `begin()` и `end()` самого
представления остались парными.

На этапе 13 разбор потока байтов — диапазон, конец которого заранее
неизвестен; «конец» там — условие, и страж выражает это прямо.

## Индекс по номеру и почему он держится

До этапа 7 STATUS, TYPE, BUMP и DELAY искали задачу перебором всех задач
всех очередей. Теперь — `std::unordered_map<TaskId, Location>`, где
`Location` — имя очереди и **итератор узла** в её дереве. Это возможно
только потому, что итераторы `std::map` не становятся недействительными ни
при вставке, ни при удалении других элементов: узлы не переезжают.
У отсортированного vector этого не было бы — любая вставка сдвигает хвост,
и индекс пришлось бы пересчитывать.

Именно поэтому `submit_batch` перестал быть copy-and-swap: копия очереди —
это новые узлы, и итераторы индекса после обмена смотрели бы в
уничтоженную копию. Теперь пакет вставляется на место с откатом —
`docs/errors.md`.

## emplace против push, try_emplace против insert

`tests/test_emplace.cpp` на типе `Traced`, который пишет в журнал каждый
вызов конструктора:

| Вызов | Что происходит |
|---|---|
| `v.push_back(Traced("x"))` | construct, move |
| `v.emplace_back("x")` | construct — на месте |
| `v.emplace_back(variable)` | construct, copy — копию lvalue убирает только `std::move` |
| `m.insert({"k", Traced("new")})` при занятом ключе | значение построено, перенесено в пару и выброшено |
| `m.try_emplace("k", "new")` при занятом ключе | ничего |
| `m.try_emplace("k", "x")` для нового | construct — один раз |
| `m["k"] = Traced("x")` | construct, default, move-assign |

### Что исправлено в коде

- **SUBMIT копировал задачу дважды.** Сервис собирал задачи пакета в вектор,
  а `submit_batch` принимал его по `const&` и копировал каждую задачу в
  очередь — клон тела ради вектора, который тут же уничтожался. Теперь
  пакет передаётся перемещением, и задача переезжает в узел дерева без
  копии.
- **Задача пакета строилась временным объектом.**
  `emplace_back(queue, Task(...))` строил `Task` и перемещал его в пару;
  `emplace_back(std::piecewise_construct, ...)` передаёт аргументы
  конструкторам обоих полей пары, и задача рождается на месте.
- **WORKER строил воркера, даже если он уже есть.** Аргументы
  `try_emplace(name, std::make_shared<Worker>(name))` вычисляются до
  вызова: выделение памяти и конструирование случались и для уже
  зарегистрированного имени. Теперь `lower_bound` находит место, воркер
  строится только для нового имени, и `emplace_hint` вставляет в найденное
  место без второго поиска.

## Ловушка operator[]

```bash
grep -rn '\]\s*=' include src
grep -rn 'operator\[\]' include src
```

Ни одного `operator[]` на ассоциативном контейнере в рабочем коде нет. Все
операции хранилища ищут очередь через `find` и на отсутствующую бросают
`UnknownQueueError`; очередь создаёт только CREATE. Опечатка в имени —
`SUBMIT biuld make` — даёт ошибку, а не очередь `biuld`, в которую никто
не посмотрит. Тесты: `TaskStore.PeekingMissingQueueCreatesNothing` (с этапа
3) и `QueueIterator.MissingQueueIsAnErrorNotAnEmptyQueue` — обход
несуществующей очереди её тоже не создаёт. В сервисе `leases_.at(id)` —
там, где отсутствие записи было бы нарушением инварианта, и `at` бросит, а
не создаст пустую аренду.

## Домашнее задание: удаление внутри обхода

`tools/invalidation_demo.cpp` — очередь `std::map` по ключу выдачи, из
которой снимаются все задачи с приоритетом ниже 5. Ошибка — `++it` после
`erase(it)`.

### Санитайзер адресов молчит

```
$ ./taskq_invalidation_demo          # сборка с -fsanitize=address
tasks left: 15
```

Программа «работает» и даже выдаёт правильное число. Инкремент итератора
`map` — функция `_Rb_tree_increment`, и она определена не в заголовке, а в
`libstdc++.so`, собранной без санитайзера: чтение освобождённого узла
происходит в коде, который санитайзер не видит.

### valgrind видит

```
$ valgrind ./taskq_invalidation_demo
==11538== Invalid read of size 8
==11538==    at 0x492CB34: std::_Rb_tree_increment(std::_Rb_tree_node_base*) (in .../libstdc++.so.6.0.34)
==11538==    by 0x10B4D6: std::_Rb_tree_iterator<std::pair<taskq::PlainTaskKey const, std::string> >::operator++() (stl_tree.h:402)
==11538==    by 0x10A392: main (invalidation_demo.cpp:33)
==11538==  Address 0x4f7f5a8 is 24 bytes inside a block of size 80 free'd
==11538==    at 0x484A61D: operator delete(void*, unsigned long)
```

Строка 33 — `++it` в заголовке цикла; блок в 80 байт — узел дерева с ключом
и строкой.

### Отладочный режим видит

```
$ ./taskq_invalidation_debug         # -D_GLIBCXX_DEBUG
.../debug/safe_iterator.h:392:
    gnu_debug::_Safe_iterator<...>::operator++()
    [with _Iterator = std::_Rb_tree_iterator<std::pair<const taskq::PlainTaskKey, std::string> >; ...]

Error: attempt to increment a singular iterator.
```

Цель `taskq_invalidation_debug` собирается всегда, тест ctest
`taskq_invalidation_detected` проверяет это сообщение. С `taskq_core` она
не связывается: отладочный режим меняет устройство контейнеров, и смешивать
его с библиотекой, собранной без него, нельзя.

Вывод: «санитайзер ничего не нашёл» — не доказательство. Он видит только
инструментированный код.

### Исправление

```cpp
for (auto it = queue.begin(); it != queue.end();) {
    if (it->first.priority < 5) {
        it = queue.erase(it);
    } else {
        ++it;
    }
}
```

Короче — `std::erase_if(queue, pred)` из C++20. Тесты —
`tests/test_invalidation.cpp`.

### Какие операции std::map делают итераторы недействительными

Контейнер очереди — `std::map`, и правила у него самые мягкие из
стандартных контейнеров:

| Операция | Итераторы и ссылки |
|---|---|
| `emplace`, `insert`, `try_emplace` (SUBMIT, RELEASE) | все действительны |
| `erase(it)`, `erase(key)` (POLL, PURGE) | только удалённого элемента |
| `extract` | только извлечённого |
| `swap` контейнеров | действительны, но смотрят в другой контейнер |
| `clear` | все |

Первые две строки — то, на чём стоит индекс по номеру: он хранит итераторы
узлов, и тысяча постановок и выдач вокруг задачи их не трогает. Тесты
`Invalidation.MapEraseAndInsertKeepOtherIterators` (адрес значения после
тысячи вставок и удаления по условию — тот же) и
`Invalidation.StoreIndexSurvivesTraffic` (задача находится по номеру после
тысячи операций вокруг неё). Строка про `swap` — та, из-за которой
copy-and-swap пакета пришлось заменить откатом.
