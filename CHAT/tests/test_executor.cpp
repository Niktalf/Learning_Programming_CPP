#include <gtest/gtest.h>

#include <string>
#include <utility>

#include "errors.hpp"
#include "executor.hpp"
#include "parser.hpp"

namespace {
    Response run(ChatStore& store, std::string text) {
        const InputLine line{std::move(text)};
        return execute(store, parse(line));
    }

    bool contains(const std::string& haystack, const std::string& needle) {
        return haystack.find(needle) != std::string::npos;
    }
}  // namespace

TEST(Executor, JoinThenMessageThenHistory) {
    ChatStore store;
    EXPECT_TRUE(run(store, "/join #general").ok);
    EXPECT_TRUE(run(store, "/msg #general hello").ok);

    const auto history = run(store, "/history #general");
    EXPECT_TRUE(history.ok);
    EXPECT_TRUE(contains(history.text, "hello"));
}

TEST(Executor, HistoryOfMissingRoomThrows) {
    ChatStore store;
    EXPECT_THROW(run(store, "/history #absent"), UnknownRoomError);
}

TEST(Executor, HistoryRespectsLimit) {
    ChatStore store;
    run(store, "/join #general");
    for (int i = 0; i < 5; ++i) {
        run(store, "/msg #general message " + std::to_string(i));
    }
    const auto [ok, text] = run(store, "/history #general 2");
    EXPECT_TRUE(ok);
    EXPECT_FALSE(contains(text, "message 0"));
    EXPECT_TRUE(contains(text, "message 4"));
}

TEST(Executor, TypeReportsAlternative) {
    ChatStore store;
    run(store, "/join #general");
    run(store, "/msg #general text");
    EXPECT_EQ(run(store, "/type #general 0").text, std::string("text"));
}

TEST(Executor, ReactOnTextThrowsTypeMismatch) {
    ChatStore store;
    run(store, "/join #general");
    run(store, "/msg #general text");

    EXPECT_THROW(run(store, "/react #general 0"), TypeMismatchError);
}

TEST(Executor, MessageToMissingRoomThrows) {
    ChatStore store;
    EXPECT_THROW(run(store, "/msg #absent hello"), UnknownRoomError);
}

TEST(Executor, ReactOnReactionAccumulates) {
    ChatStore store;
    store.post("#general", Message("me", Body {
        Reaction {
            .emoji = "+1",
            .count = 0
        }
    }));
    EXPECT_EQ(run(store, "/react #general 0").text, std::string("1"));
    EXPECT_EQ(run(store, "/react #general 0 4").text, std::string("5"));
}

TEST(Executor, MissingMessageIndexThrows) {
    ChatStore store;
    run(store, "/join #general");
    run(store, "/msg #general text");
    EXPECT_THROW(run(store, "/type #general 99"), ChatError);
}
