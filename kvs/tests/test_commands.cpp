#include <gtest/gtest.h>

#include <exception>
#include <string>

#include "kvs/commands.hpp"
#include "kvs/errors.hpp"

// Команды хранилища через реестр этапа 6. До него эти тесты назывались
// test_executor и шли через parse и execute; поведение команд то же.

using kvs::Response;
using kvs::Store;

namespace {

// Исполняет команду напрямую, без границы: исключения должны долетать до
// теста целыми. Так проверяется правило «внутри ядра только бросают».
Response run(Store& store, const std::string& text) {
    return kvs::execute(store, text);
}

// Связанная команда оборачивает любую ошибку контекстом (звёздочка этапа 3),
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

TEST(Commands, SetThenGet) {
    Store store;
    EXPECT_TRUE(run(store, "SET k hello").ok);
    const auto got = run(store, "GET k");
    EXPECT_TRUE(got.ok);
    EXPECT_EQ(got.text, std::string("hello"));
}

TEST(Commands, GetMissingKeyThrows) {
    Store store;
    EXPECT_TRUE(root_if<kvs::KeyNotFoundError>(store, "GET absent"));
}

TEST(Commands, ExistsReportsPresence) {
    Store store;
    EXPECT_EQ(run(store, "EXISTS k").text, std::string("0"));
    run(store, "SET k v");
    EXPECT_EQ(run(store, "EXISTS k").text, std::string("1"));
}

TEST(Commands, TypeReportsAlternative) {
    Store store;
    run(store, "SET s text");
    run(store, "INCR n 5");
    EXPECT_EQ(run(store, "TYPE s").text, std::string("string"));
    EXPECT_EQ(run(store, "TYPE n").text, std::string("int"));
}

TEST(Commands, IncrCreatesAndAccumulates) {
    Store store;
    EXPECT_EQ(run(store, "INCR n").text, std::string("1"));
    EXPECT_EQ(run(store, "INCR n 41").text, std::string("42"));
}

TEST(Commands, IncrOnStringThrowsTypeMismatch) {
    Store store;
    run(store, "SET s text");
    // Этап 2 возвращал здесь флаг. Теперь это исключение с контекстом в полях,
    // и ловит его не обработчик, а граница Server::handle — см. test_server.cpp.
    const std::exception_ptr root = root_if<kvs::TypeMismatchError>(store, "INCR s");
    ASSERT_TRUE(root);
    try {
        std::rethrow_exception(root);
    } catch (const kvs::TypeMismatchError& e) {
        EXPECT_EQ(e.key(), std::string("s"));
        EXPECT_EQ(e.actual(), std::string("string"));
    }
}

TEST(Commands, MsetWritesAllPairs) {
    Store store;
    run(store, "MSET a 1 b 2 c 3");
    EXPECT_EQ(store.size(), 3u);
    EXPECT_EQ(run(store, "GET b").text, std::string("2"));
}

TEST(Commands, QuotedValueKeepsSpaces) {
    Store store;
    run(store, R"(SET greeting "hello world")");
    EXPECT_EQ(run(store, "GET greeting").text, std::string("hello world"));
}

TEST(Commands, ErrorCarriesCommandContext) {
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

TEST(Commands, IncrOverflowIsAnErrorNotUndefinedBehaviour) {
    Store store;
    run(store, "INCR n 9223372036854775807");
    EXPECT_TRUE(root_if<kvs::OverflowError>(store, "INCR n"));
    EXPECT_EQ(run(store, "GET n").text, std::string("9223372036854775807"));
    run(store, "INCR m -9223372036854775807");
    EXPECT_EQ(run(store, "INCR m -1").text, std::string("-9223372036854775808"));
    EXPECT_TRUE(root_if<kvs::OverflowError>(store, "INCR m -1"));
}

TEST(Commands, MsetRejectsOddArgumentCount) {
    // Арность «не меньше двух» реестр выводит из Rest; чётность — правило
    // самой команды, и проверяет её обработчик.
    Store store;
    EXPECT_TRUE(root_if<kvs::ParseError>(store, "MSET a 1 b"));
    EXPECT_EQ(store.size(), 0u);
}

TEST(Commands, ErrorContextIsVerbAndFirstArgument) {
    Store store;
    try {
        run(store, "MSET a 1 b");
        ADD_FAILURE() << "odd MSET must throw";
    } catch (...) {
        const auto context = kvs::context_of(std::current_exception());
        ASSERT_EQ(context.size(), 1u);
        EXPECT_EQ(context.front(), std::string("while executing MSET a"));
    }
}

TEST(Commands, KeysAndVersionGoThroughTheRegistry) {
    Store store;
    run(store, "MSET user:1 a user:2 b other c");
    EXPECT_EQ(run(store, "KEYS user:*").text, std::string("user:1\nuser:2"));
    EXPECT_EQ(run(store, "KEYS user:* 1").text, std::string("user:1"));
    EXPECT_EQ(run(store, "KEYS nothing*").text, std::string("(empty)"));
    EXPECT_TRUE(run(store, "VERSION other").text.starts_with("generation=1 "));
}

// --- Домашнее задание этапа 6 -------------------------------------------

TEST(Commands, AppendCreatesAndExtends) {
    Store store;
    EXPECT_EQ(run(store, "APPEND s hello").text, std::string("5"));
    EXPECT_EQ(run(store, R"(APPEND s " world")").text, std::string("11"));
    EXPECT_EQ(run(store, "GET s").text, std::string("hello world"));
}

TEST(Commands, AppendToNonStringIsTypeMismatch) {
    Store store;
    run(store, "INCR n");
    EXPECT_TRUE(root_if<kvs::TypeMismatchError>(store, "APPEND n x"));
    EXPECT_EQ(run(store, "GET n").text, std::string("1"));
}

TEST(Commands, StrlenOfAbsentKeyIsZero) {
    Store store;
    EXPECT_EQ(run(store, "STRLEN absent").text, std::string("0"));
    run(store, R"(SET s "four")");
    EXPECT_EQ(run(store, "STRLEN s").text, std::string("4"));
    run(store, "RPUSH l x");
    EXPECT_TRUE(root_if<kvs::TypeMismatchError>(store, "STRLEN l"));
}

TEST(Commands, GetsetReturnsPreviousValue) {
    Store store;
    EXPECT_EQ(run(store, "GETSET k first").text, std::string("(nil)"));
    EXPECT_EQ(run(store, "GETSET k second").text, std::string("first"));
    EXPECT_EQ(run(store, "GET k").text, std::string("second"));
}

TEST(Commands, GetsetIsOneStoreOperation) {
    // Одна операция хранилища — одна новая версия записи. Пара get + set
    // тоже дала бы одну, но exchange — единственное место, которое на
    // этапе 11 окажется под одним захватом мьютекса.
    Store store;
    run(store, "SET k v");
    const auto before = store.version("k");
    static_cast<void>(store.exchange("k", kvs::Value(std::string_view("w"))));
    const auto after = store.version("k");
    ASSERT_TRUE(before && after);
    EXPECT_EQ(after->generation, before->generation + 1);
}

TEST(Commands, GetsetOverMemoryLimitChangesNothing) {
    // Сильная гарантия exchange: запись не прошла — старое значение на месте.
    Store store(64);
    run(store, "SET k small");
    EXPECT_TRUE(root_if<kvs::OutOfMemoryError>(store, "GETSET k " + std::string(200, 'x')));
    EXPECT_EQ(run(store, "GET k").text, std::string("small"));
}
