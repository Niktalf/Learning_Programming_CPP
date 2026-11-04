#include <gtest/gtest.h>

#include <exception>
#include <string>
#include <utility>

#include "kvs/errors.hpp"
#include "kvs/executor.hpp"
#include "kvs/parser.hpp"

using kvs::InputLine;
using kvs::Response;
using kvs::Store;

namespace {

// Вызывает исполнитель напрямую, без границы: исключения должны долетать
// до теста целыми. Так проверяется правило «внутри ядра только бросают».
Response run(Store& store, std::string text) {
    const InputLine line{std::move(text)};
    return kvs::execute(store, kvs::parse(line));
}

// Исполнитель оборачивает любую ошибку контекстом команды (звёздочка этапа 3),
// поэтому тест проверяет исходную причину внутри цепочки. Возвращает её,
// если она нужного типа, и пустой указатель иначе.
template <class E>
std::exception_ptr root_if(Store& store, const std::string& text) {
    try {
        run(store, text);
    } catch (...) {
        const std::exception_ptr root = kvs::root_cause(std::current_exception());
        try {
            std::rethrow_exception(root);
        } catch (const E&) {
            return root;
        } catch (...) {
            return nullptr;
        }
    }
    return nullptr;
}

}  // namespace

TEST(Executor, SetThenGet) {
    Store store;
    EXPECT_TRUE(run(store, "SET k hello").ok);
    const auto got = run(store, "GET k");
    EXPECT_TRUE(got.ok);
    EXPECT_EQ(got.text, std::string("hello"));
}

TEST(Executor, GetMissingKeyThrows) {
    Store store;
    EXPECT_TRUE(root_if<kvs::KeyNotFoundError>(store, "GET absent"));
}

TEST(Executor, ExistsReportsPresence) {
    Store store;
    EXPECT_EQ(run(store, "EXISTS k").text, std::string("0"));
    run(store, "SET k v");
    EXPECT_EQ(run(store, "EXISTS k").text, std::string("1"));
}

TEST(Executor, TypeReportsAlternative) {
    Store store;
    run(store, "SET s text");
    run(store, "INCR n 5");
    EXPECT_EQ(run(store, "TYPE s").text, std::string("string"));
    EXPECT_EQ(run(store, "TYPE n").text, std::string("int"));
}

TEST(Executor, IncrCreatesAndAccumulates) {
    Store store;
    EXPECT_EQ(run(store, "INCR n").text, std::string("1"));
    EXPECT_EQ(run(store, "INCR n 41").text, std::string("42"));
}

TEST(Executor, IncrOnStringThrowsTypeMismatch) {
    Store store;
    run(store, "SET s text");
    // Этап 2 возвращал здесь флаг. Теперь это исключение с контекстом в полях,
    // и ловит его не исполнитель, а граница Server::handle — см. test_server.cpp.
    const std::exception_ptr root = root_if<kvs::TypeMismatchError>(store, "INCR s");
    ASSERT_TRUE(root);
    try {
        std::rethrow_exception(root);
    } catch (const kvs::TypeMismatchError& e) {
        EXPECT_EQ(e.key(), std::string("s"));
        EXPECT_EQ(e.actual(), std::string("string"));
    }
}

TEST(Executor, MsetWritesAllPairs) {
    Store store;
    run(store, "MSET a 1 b 2 c 3");
    EXPECT_EQ(store.size(), 3u);
    EXPECT_EQ(run(store, "GET b").text, std::string("2"));
}

TEST(Executor, QuotedValueKeepsSpaces) {
    Store store;
    run(store, R"(SET greeting "hello world")");
    EXPECT_EQ(run(store, "GET greeting").text, std::string("hello world"));
}

TEST(Executor, ErrorCarriesCommandContext) {
    Store store;
    try {
        run(store, "GET absent");
        ADD_FAILURE() << "GET of a missing key must throw";
    } catch (...) {
        const auto context = kvs::context_of(std::current_exception());
        ASSERT_EQ(context.size(), 1u);
        EXPECT_EQ(context.front(), std::string("while executing GET absent"));
    }
}

TEST(Executor, IncrOverflowIsAnErrorNotUndefinedBehaviour) {
    Store store;
    run(store, "INCR n 9223372036854775807");
    EXPECT_TRUE(root_if<kvs::OverflowError>(store, "INCR n"));
    EXPECT_EQ(run(store, "GET n").text, std::string("9223372036854775807"));
    run(store, "INCR m -9223372036854775807");
    EXPECT_EQ(run(store, "INCR m -1").text, std::string("-9223372036854775808"));
    EXPECT_TRUE(root_if<kvs::OverflowError>(store, "INCR m -1"));
}
