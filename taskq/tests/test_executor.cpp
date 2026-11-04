#include <gtest/gtest.h>

#include <exception>
#include <string>

#include "taskq/errors.hpp"
#include "taskq/executor.hpp"
#include "taskq/parser.hpp"

using taskq::Response;
using taskq::Service;
using taskq::State;

namespace {

// Исполняет одну строку без границы: ошибка вылетает наружу исключением.
// Строка живёт внутри функции, ответ — независимая копия.
Response run(Service& service, const std::string& text) {
    const taskq::InputLine line{text};
    return service.execute(taskq::parse(line));
}

// Исходная причина. Исполнитель оборачивает каждую ошибку звеном контекста,
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

TEST(Executor, SubmitAssignsIncreasingIds) {
    Service service = with_build_queue();
    EXPECT_EQ(run(service, "SUBMIT build make all").text, std::string("OK id=1"));
    EXPECT_EQ(run(service, "SUBMIT build make test").text, std::string("OK id=2"));
}

TEST(Executor, SubmitToUnknownQueueIsAnError) {
    Service service;
    EXPECT_TRUE(fails_with<taskq::UnknownQueueError>(service, "SUBMIT absent x"));
    // Неудачная постановка номер не тратит.
    static_cast<void>(run(service, "CREATE build"));
    EXPECT_EQ(run(service, "SUBMIT build x").text, std::string("OK id=1"));
}

TEST(Executor, PollLeasesInFifoOrderAndAckFinishes) {
    Service service = with_build_queue();
    static_cast<void>(run(service, "SUBMIT build first"));
    static_cast<void>(run(service, "SUBMIT build second"));

    EXPECT_EQ(run(service, "POLL build").text, std::string("id=1 first"));
    EXPECT_EQ(run(service, "STATUS 1").text, std::string("leased"));
    EXPECT_EQ(run(service, "STATUS 2").text, std::string("pending in build"));

    EXPECT_TRUE(run(service, "ACK 1").ok);
    EXPECT_EQ(run(service, "STATUS 1").text, std::string("done"));
}

TEST(Executor, EmptyQueueAndMissingQueueAreDifferent) {
    Service service = with_build_queue();
    // Изъян этапа 2 закрыт: пустая очередь — ответ, отсутствующая — ошибка.
    EXPECT_EQ(run(service, "POLL build").text, std::string("(empty)"));
    EXPECT_TRUE(fails_with<taskq::UnknownQueueError>(service, "POLL absent"));
}

TEST(Executor, FailMarksTaskFailed) {
    Service service = with_build_queue();
    static_cast<void>(run(service, "SUBMIT build x"));
    static_cast<void>(run(service, "POLL build"));
    EXPECT_TRUE(run(service, "FAIL 1").ok);
    EXPECT_EQ(run(service, "STATUS 1").text, std::string("failed"));
}

TEST(Executor, TransitionsFollowTheAutomaton) {
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

TEST(Executor, InvalidTransitionCarriesStates) {
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

TEST(Executor, UnknownTaskIsNotAnInvalidTransition) {
    // 404 и 409 на этапе 16 — разные ответы, и клиент обязан их различать.
    Service service = with_build_queue();
    EXPECT_TRUE(fails_with<taskq::UnknownTaskError>(service, "ACK 42"));
    EXPECT_TRUE(fails_with<taskq::UnknownTaskError>(service, "STATUS 42"));
}

TEST(Executor, ReleaseReturnsLeaseToTheEndOfItsQueue) {
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

TEST(Executor, TypeAndBumpDependOnState) {
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

TEST(Executor, BumpOverflowIsAnErrorNotUndefinedBehaviour) {
    Service service = with_build_queue();
    static_cast<void>(run(service, "SUBMIT build num:9223372036854775806"));
    EXPECT_EQ(run(service, "BUMP 1").text, std::string("9223372036854775807"));
    EXPECT_TRUE(fails_with<taskq::OverflowError>(service, "BUMP 1"));
    EXPECT_EQ(run(service, "BUMP 1 0").text, std::string("9223372036854775807"));
}

TEST(Executor, MalformedPayloadIsParseError) {
    Service service = with_build_queue();
    EXPECT_TRUE(fails_with<taskq::ParseError>(service, "SUBMIT build num:12abc"));
    EXPECT_TRUE(fails_with<taskq::ParseError>(service, "SUBMIT build bin:abc"));
}

TEST(Executor, BatchSubmitsAllWithConsecutiveIds) {
    Service service = with_build_queue();
    static_cast<void>(run(service, "CREATE deploy"));

    EXPECT_EQ(run(service, R"(SUBMITBATCH build "make all" deploy ship build test)").text,
              std::string("OK ids=1..3"));
    EXPECT_EQ(run(service, "STATUS 2").text, std::string("pending in deploy"));
    EXPECT_EQ(run(service, "STATUS 3").text, std::string("pending in build"));
}

TEST(Executor, BatchWithUnknownQueueSubmitsNothing) {
    Service service = with_build_queue();
    EXPECT_TRUE(fails_with<taskq::UnknownQueueError>(service, "SUBMITBATCH build a absent b build c"));
    EXPECT_EQ(run(service, "POLL build").text, std::string("(empty)"));
    EXPECT_TRUE(fails_with<taskq::UnknownTaskError>(service, "STATUS 1"));
    // Номера не потрачены.
    EXPECT_EQ(run(service, "SUBMIT build x").text, std::string("OK id=1"));
}

TEST(Executor, ErrorCarriesCommandContext) {
    Service service;
    try {
        static_cast<void>(run(service, "ACK 7"));
        ADD_FAILURE() << "must throw";
    } catch (const taskq::TaskQError& e) {
        // Внешнее звено — контекст, внутреннее — причина.
        EXPECT_EQ(std::string(e.what()), std::string("while executing ACK 7"));
    }
}
