#include <gtest/gtest.h>

#include <chrono>
#include <exception>
#include <memory>
#include <string>
#include <string_view>

#include "taskq/errors.hpp"
#include "taskq/service.hpp"

// Команды сервиса через реестр этапа 6. До него эти тесты назывались
// test_executor и шли через parse и execute; поведение команд то же. Тесты
// разбора из бывшего test_parser и домашнее задание — в конце файла.

using taskq::Response;
using taskq::Service;
using taskq::State;

namespace {

// Исполняет одну строку без границы: ошибка вылетает наружу исключением.
// Строка живёт всё время вызова, ответ — независимая копия.
Response run(Service& service, const std::string& text) {
    return service.execute(text);
}

// Исходная причина. Связанная команда оборачивает каждую ошибку звеном контекста,
// поэтому тип проверяется по корню цепочки, а не по внешнему исключению.
template <class E>
bool fails_with(Service& service, const std::string& text) {
    try {
        static_cast<void>(run(service, text));
    } catch (...) {
        try {
            std::rethrow_exception(taskq::root_cause(std::current_exception()));
        } catch (const E&) {
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

// Сервис с очередью build — почти каждому тесту нужна ровно такая.
Service with_build_queue() {
    Service service;
    static_cast<void>(run(service, "CREATE build"));
    return service;
}

}  // namespace

TEST(Commands, SubmitAssignsIncreasingIds) {
    Service service = with_build_queue();
    EXPECT_EQ(run(service, "SUBMIT build make all").text, std::string("OK id=1"));
    EXPECT_EQ(run(service, "SUBMIT build make test").text, std::string("OK id=2"));
}

TEST(Commands, SubmitToUnknownQueueIsAnError) {
    Service service;
    EXPECT_TRUE(fails_with<taskq::UnknownQueueError>(service, "SUBMIT absent x"));
    // Неудачная постановка номер не тратит.
    static_cast<void>(run(service, "CREATE build"));
    EXPECT_EQ(run(service, "SUBMIT build x").text, std::string("OK id=1"));
}

TEST(Commands, PollLeasesInFifoOrderAndAckFinishes) {
    Service service = with_build_queue();
    static_cast<void>(run(service, "SUBMIT build first"));
    static_cast<void>(run(service, "SUBMIT build second"));

    EXPECT_EQ(run(service, "POLL build").text, std::string("id=1 first"));
    EXPECT_EQ(run(service, "STATUS 1").text, std::string("leased"));
    EXPECT_EQ(run(service, "STATUS 2").text, std::string("pending in build"));

    EXPECT_TRUE(run(service, "ACK 1").ok);
    EXPECT_EQ(run(service, "STATUS 1").text, std::string("done"));
}

TEST(Commands, EmptyQueueAndMissingQueueAreDifferent) {
    Service service = with_build_queue();
    // Изъян этапа 2 закрыт: пустая очередь — ответ, отсутствующая — ошибка.
    EXPECT_EQ(run(service, "POLL build").text, std::string("(empty)"));
    EXPECT_TRUE(fails_with<taskq::UnknownQueueError>(service, "POLL absent"));
}

TEST(Commands, FailMarksTaskFailed) {
    Service service = with_build_queue();
    static_cast<void>(run(service, "SUBMIT build x"));
    static_cast<void>(run(service, "POLL build"));
    EXPECT_TRUE(run(service, "FAIL 1").ok);
    EXPECT_EQ(run(service, "STATUS 1").text, std::string("failed"));
}

TEST(Commands, TransitionsFollowTheAutomaton) {
    Service service = with_build_queue();
    static_cast<void>(run(service, "SUBMIT build x"));

    // Ожидающую задачу нельзя ни подтвердить, ни провалить, ни вернуть.
    EXPECT_TRUE(fails_with<taskq::InvalidTransitionError>(service, "ACK 1"));
    EXPECT_TRUE(fails_with<taskq::InvalidTransitionError>(service, "FAIL 1"));
    EXPECT_TRUE(fails_with<taskq::InvalidTransitionError>(service, "RELEASE 1"));

    static_cast<void>(run(service, "POLL build"));
    static_cast<void>(run(service, "ACK 1"));
    // Завершённая задача остаётся завершённой: Done -> Done запрещён.
    EXPECT_TRUE(fails_with<taskq::InvalidTransitionError>(service, "ACK 1"));
    EXPECT_TRUE(fails_with<taskq::InvalidTransitionError>(service, "FAIL 1"));
}

TEST(Commands, InvalidTransitionCarriesStates) {
    Service service = with_build_queue();
    static_cast<void>(run(service, "SUBMIT build x"));
    try {
        static_cast<void>(run(service, "ACK 1"));
        ADD_FAILURE() << "must throw";
    } catch (...) {
        try {
            std::rethrow_exception(taskq::root_cause(std::current_exception()));
        } catch (const taskq::InvalidTransitionError& e) {
            EXPECT_EQ(e.id(), 1u);
            EXPECT_EQ(e.from(), State::Pending);
            EXPECT_EQ(e.to(), State::Done);
        }
    }
}

TEST(Commands, UnknownTaskIsNotAnInvalidTransition) {
    // 404 и 409 на этапе 16 — разные ответы, и клиент обязан их различать.
    Service service = with_build_queue();
    EXPECT_TRUE(fails_with<taskq::UnknownTaskError>(service, "ACK 42"));
    EXPECT_TRUE(fails_with<taskq::UnknownTaskError>(service, "STATUS 42"));
}

TEST(Commands, ReleaseReturnsLeaseToTheEndOfItsQueue) {
    Service service = with_build_queue();
    static_cast<void>(run(service, "SUBMIT build first"));
    static_cast<void>(run(service, "SUBMIT build second"));
    static_cast<void>(run(service, "POLL build"));

    EXPECT_TRUE(run(service, "RELEASE 1").ok);
    EXPECT_EQ(run(service, "STATUS 1").text, std::string("pending in build"));
    // Вернувшаяся задача встаёт в конец: первой теперь выдаётся вторая.
    EXPECT_EQ(run(service, "POLL build").text, std::string("id=2 second"));
    EXPECT_EQ(run(service, "POLL build").text, std::string("id=1 first"));
}

TEST(Commands, TypeAndBumpDependOnState) {
    Service service = with_build_queue();
    static_cast<void>(run(service, "SUBMIT build num:40"));
    static_cast<void>(run(service, "SUBMIT build make"));

    EXPECT_EQ(run(service, "TYPE 1").text, std::string("number"));
    EXPECT_EQ(run(service, "BUMP 1 2").text, std::string("42"));
    EXPECT_TRUE(fails_with<taskq::TypeMismatchError>(service, "BUMP 2"));

    static_cast<void>(run(service, "POLL build"));
    EXPECT_EQ(run(service, "TYPE 1").text, std::string("number"));   // выдана, тело при ней
    EXPECT_TRUE(fails_with<taskq::WrongStateError>(service, "BUMP 1"));

    static_cast<void>(run(service, "ACK 1"));
    EXPECT_TRUE(fails_with<taskq::WrongStateError>(service, "TYPE 1"));
}

TEST(Commands, BumpOverflowIsAnErrorNotUndefinedBehaviour) {
    Service service = with_build_queue();
    static_cast<void>(run(service, "SUBMIT build num:9223372036854775806"));
    EXPECT_EQ(run(service, "BUMP 1").text, std::string("9223372036854775807"));
    EXPECT_TRUE(fails_with<taskq::OverflowError>(service, "BUMP 1"));
    EXPECT_EQ(run(service, "BUMP 1 0").text, std::string("9223372036854775807"));
}

TEST(Commands, MalformedPayloadIsParseError) {
    Service service = with_build_queue();
    EXPECT_TRUE(fails_with<taskq::ParseError>(service, "SUBMIT build num:12abc"));
    EXPECT_TRUE(fails_with<taskq::ParseError>(service, "SUBMIT build bin:abc"));
}

TEST(Commands, BatchSubmitsAllWithConsecutiveIds) {
    Service service = with_build_queue();
    static_cast<void>(run(service, "CREATE deploy"));

    EXPECT_EQ(run(service, R"(SUBMITBATCH build "make all" deploy ship build test)").text,
              std::string("OK ids=1..3"));
    EXPECT_EQ(run(service, "STATUS 2").text, std::string("pending in deploy"));
    EXPECT_EQ(run(service, "STATUS 3").text, std::string("pending in build"));
}

TEST(Commands, BatchWithUnknownQueueSubmitsNothing) {
    Service service = with_build_queue();
    EXPECT_TRUE(fails_with<taskq::UnknownQueueError>(service, "SUBMITBATCH build a absent b build c"));
    EXPECT_EQ(run(service, "POLL build").text, std::string("(empty)"));
    EXPECT_TRUE(fails_with<taskq::UnknownTaskError>(service, "STATUS 1"));
    // Номера не потрачены.
    EXPECT_EQ(run(service, "SUBMIT build x").text, std::string("OK id=1"));
}

TEST(Commands, ErrorCarriesCommandContext) {
    Service service;
    try {
        static_cast<void>(run(service, "ACK 7"));
        ADD_FAILURE() << "must throw";
    } catch (const taskq::TaskQError& e) {
        // Внешнее звено — контекст, внутреннее — причина.
        EXPECT_EQ(std::string(e.what()), std::string("while executing ACK 7"));
    }
}

// --- Разбор: бывший test_parser ------------------------------------------

namespace {

// Причина отказа разбора — по корню цепочки: ошибку аргумента реестр
// оборачивает подсказкой «usage: …». Пустая строка — разбор прошёл.
std::string reason_for(const std::string& text) {
    try {
        static_cast<void>(Service::commands().bind(text));
    } catch (...) {
        try {
            std::rethrow_exception(taskq::root_cause(std::current_exception()));
        } catch (const taskq::ParseError& e) {
            return e.reason();
        } catch (...) {
            return "(not a parse error)";
        }
    }
    return {};
}

// Подсказка, приклеенная к ошибке разбора, или пустая строка.
std::string usage_for(const std::string& text) {
    try {
        static_cast<void>(Service::commands().bind(text));
    } catch (...) {
        const auto context = taskq::context_of(std::current_exception());
        return context.empty() ? std::string() : context.front();
    }
    return {};
}

// Управляемые часы: тест двигает время сам, без sleep.
struct ManualClock {
    std::shared_ptr<taskq::Timestamp> now =
        std::make_shared<taskq::Timestamp>(std::chrono::system_clock::now());

    void advance(std::chrono::seconds by) const { *now += by; }
    taskq::Timestamp operator()() const { return *now; }
};

}  // namespace

TEST(Commands, SubmitWithQuotedQueueAndPriority) {
    Service service;
    run(service, R"(CREATE "build tasks")");
    EXPECT_EQ(run(service, R"(SUBMIT "build tasks" prio=5 make all)").text, std::string("OK id=1"));
    EXPECT_EQ(run(service, R"(PEEK "build tasks")").text, std::string("id=1 prio=5 make all"));
}

TEST(Commands, SubmitPriorityIsOptionalAndBodyIsVerbatim) {
    Service service = with_build_queue();
    run(service, "submit build make   -j8 all");   // регистр не важен
    run(service, R"(SUBMIT build echo "hi)");      // кавычки в теле — просто символы
    EXPECT_EQ(run(service, "LIST build").text,
              std::string("id=1 prio=0 make   -j8 all\nid=2 prio=0 echo \"hi"));
}

TEST(Commands, PriorityOutOfRangeOrGarbageIsRejected) {
    EXPECT_EQ(reason_for("SUBMIT build prio=10 x"), std::string("priority must be 0..9: 10"));
    EXPECT_EQ(reason_for("SUBMIT build prio=5x x"), std::string("priority must be 0..9: 5x"));
    EXPECT_EQ(reason_for("SUBMIT build prio=5"), std::string("missing <body...>"));
}

TEST(Commands, RejectsMalformedInput) {
    for (const char* const text :
         {"", "   ", "SUBMIT", "SUBMIT build", "POLL", "ACK", "ACK abc", "ACK 1 2", "ACK -1",
          "BUMP 1 x", R"(SUBMIT "build 5 make)", "UNKNOWN 1", "hello", "CREATE", "SUBMITBATCH",
          "SUBMITBATCH build", "SUBMITBATCH build prio=3", "REAP now", "DELAY 1", "DELAY 1 soon",
          "PURGE", "PEEK a b"}) {
        EXPECT_NE(reason_for(text), std::string()) << text;
    }
}

TEST(Commands, ErrorCarriesReason) {
    EXPECT_EQ(reason_for("FROB 1"), std::string("unknown command: FROB"));
    EXPECT_EQ(reason_for("ACK abc"), std::string("task id must be a positive number: abc"));
    EXPECT_EQ(reason_for("ACK 1 2"), std::string("unexpected argument: 2"));
    EXPECT_EQ(reason_for(R"(SUBMIT "   " x)"), std::string("queue name is empty"));
    EXPECT_EQ(reason_for(R"(CREATE " build")"), std::string("queue name starts or ends with a space"));
}

TEST(Commands, TaskIdFormatIsChecked) {
    // TaskId — тот же uint64_t, но разбирается своей веткой convert, выше
    // ветки для чисел: знак, ведущие нули и ноль — не номер задачи.
    EXPECT_EQ(reason_for("ACK 7"), std::string());
    for (const char* const text : {"ACK +7", "ACK 007", "ACK 0", "ACK 7abc", "ACK -1"}) {
        EXPECT_EQ(reason_for(text).rfind("task id must be a positive number: ", 0), 0u) << text;
    }
}

TEST(Commands, UsageComesFromTheSignature) {
    // Сообщение «not a number: sleeping» само по себе не говорит, где
    // ошибка; подсказка из сигнатуры говорит.
    EXPECT_EQ(usage_for("ACK sleeping"), std::string("usage: ACK <id>"));
    EXPECT_EQ(usage_for("SUBMIT build"), std::string("usage: SUBMIT <queue> [prio=0..9] <body...>"));
    EXPECT_EQ(usage_for("BUMP 1 x"), std::string("usage: BUMP <id> [number]"));
    EXPECT_EQ(usage_for("POLL"), std::string("usage: POLL <queue> [worker]"));
    EXPECT_EQ(usage_for("REAP now"), std::string("usage: REAP"));
    EXPECT_EQ(usage_for("DELAY 1"), std::string("usage: DELAY <id> <delay>"));
    EXPECT_EQ(usage_for("LIST"),
              std::string("usage: LIST <queue> [prio>=0..9] [type=text|number|binary] [limit=N]"));
    EXPECT_EQ(usage_for("SUBMITBATCH"),
              std::string("usage: SUBMITBATCH <queue> [prio=0..9] <body> ..."));
}

TEST(Commands, BatchOfItemsWithOptionalPriority) {
    Service service;
    run(service, "CREATE build");
    run(service, "CREATE deploy");
    EXPECT_EQ(run(service, R"(SUBMITBATCH build "make all" deploy prio=7 "ship it" build num:3)").text,
              std::string("OK ids=1..3"));
    EXPECT_EQ(run(service, "LIST deploy").text, std::string("id=2 prio=7 ship it"));
    EXPECT_EQ(run(service, "TYPE 3").text, std::string("number"));
    EXPECT_EQ(reason_for("SUBMITBATCH build x deploy"),
              std::string("SUBMITBATCH item for deploy has no body"));
}

TEST(Commands, NamedArgumentsInAnyOrder) {
    Service service = with_build_queue();
    run(service, "SUBMIT build a");
    run(service, "SUBMIT build prio=5 b");
    run(service, "SUBMIT build prio=5 num:1");
    const std::string expected = "id=2 prio=5 b";
    EXPECT_EQ(run(service, "LIST build prio>=5 limit=1").text, expected);
    EXPECT_EQ(run(service, "LIST build limit=1 prio>=5").text, expected);
    EXPECT_EQ(run(service, "LIST build type=number").text, std::string("id=3 prio=5 1"));
    EXPECT_EQ(reason_for("LIST build limit=1 limit=2"), std::string("unexpected argument: limit=2"));
    EXPECT_EQ(reason_for("LIST build type=json"),
              std::string("type must be text, number or binary: json"));
}

TEST(Commands, ReadOnlyCommandsAreConstMethods) {
    // Константность обработчика видна реестру: STATUS — константный метод.
    EXPECT_TRUE(Service::commands().bind("STATUS 1").reads_only());
    EXPECT_TRUE(Service::commands().bind("PEEK build").reads_only());
    EXPECT_FALSE(Service::commands().bind("ACK 1").reads_only());
}

// --- Домашнее задание этапа 6 -------------------------------------------

TEST(Commands, PeekShowsWhatPollWouldLease) {
    Service service = with_build_queue();
    EXPECT_EQ(run(service, "PEEK build").text, std::string("(empty)"));
    run(service, "SUBMIT build low");
    run(service, "SUBMIT build prio=9 high");
    EXPECT_EQ(run(service, "PEEK build").text, std::string("id=2 prio=9 high"));
    EXPECT_EQ(run(service, "STATUS 2").text, std::string("pending in build"));   // PEEK не выдаёт
    EXPECT_EQ(run(service, "POLL build").text, std::string("id=2 high"));
    EXPECT_TRUE(fails_with<taskq::UnknownQueueError>(service, "PEEK absent"));
}

TEST(Commands, DelayedTaskIsSkippedUntilItsTime) {
    Service service = with_build_queue();
    const ManualClock clock;
    service.set_clock(clock);
    run(service, "SUBMIT build prio=9 urgent");
    run(service, "SUBMIT build routine");

    EXPECT_EQ(run(service, "DELAY 1 90").text, std::string("OK, delayed for 90s"));
    EXPECT_EQ(run(service, "STATUS 1").text, std::string("pending in build, delayed for 90s"));
    // Отложенная задача с высшим приоритетом не загораживает очередь:
    // выдаётся следующая готовая.
    EXPECT_EQ(run(service, "PEEK build").text, std::string("id=2 prio=0 routine"));
    EXPECT_EQ(run(service, "POLL build").text, std::string("id=2 routine"));
    EXPECT_EQ(run(service, "POLL build").text, std::string("(empty)"));

    clock.advance(std::chrono::seconds(90));
    EXPECT_EQ(run(service, "STATUS 1").text, std::string("pending in build"));
    EXPECT_EQ(run(service, "POLL build").text, std::string("id=1 urgent"));
}

TEST(Commands, DelayUnitsAndRules) {
    Service service = with_build_queue();
    const ManualClock clock;
    service.set_clock(clock);
    run(service, "SUBMIT build x");
    EXPECT_EQ(run(service, "DELAY 1 15m").text, std::string("OK, delayed for 900s"));
    EXPECT_EQ(run(service, "DELAY 1 2h").text, std::string("OK, delayed for 7200s"));
    EXPECT_EQ(run(service, "DELAY 1 0").text, std::string("OK, delayed for 0s"));   // снять задержку
    EXPECT_EQ(run(service, "PEEK build").text, std::string("id=1 prio=0 x"));
    EXPECT_EQ(reason_for("DELAY 1 -5"),
              std::string("delay must be like 90, 90s, 15m or 2h, at most a year: -5"));
    EXPECT_EQ(reason_for("DELAY 1 9000h"),
              std::string("delay must be like 90, 90s, 15m or 2h, at most a year: 9000h"));
    // Отложить можно только ожидающую задачу.
    run(service, "POLL build");
    EXPECT_TRUE(fails_with<taskq::WrongStateError>(service, "DELAY 1 10"));
    EXPECT_TRUE(fails_with<taskq::UnknownTaskError>(service, "DELAY 99 10"));
}

TEST(Commands, PurgeCancelsPendingTasksOnly) {
    Service service = with_build_queue();
    run(service, "SUBMIT build a");
    run(service, "SUBMIT build b");
    run(service, "SUBMIT build c");
    run(service, "POLL build");   // задача 1 у воркера

    EXPECT_EQ(run(service, "PURGE build").text, std::string("OK purged=2"));
    EXPECT_EQ(run(service, "STATUS 1").text, std::string("leased"));
    EXPECT_EQ(run(service, "STATUS 2").text, std::string("cancelled"));
    EXPECT_EQ(run(service, "STATUS 3").text, std::string("cancelled"));
    EXPECT_EQ(run(service, "LIST build").text, std::string("(empty)"));
    // Cancelled — конечное состояние. Возврат аренды в очередь работает
    // по-прежнему: PURGE очистил очередь, но не закрыл её.
    run(service, "RELEASE 1");
    EXPECT_EQ(run(service, "PEEK build").text, std::string("id=1 prio=0 a"));
    EXPECT_EQ(run(service, "PURGE build").text, std::string("OK purged=1"));
    EXPECT_TRUE(fails_with<taskq::UnknownQueueError>(service, "PURGE absent"));
}
