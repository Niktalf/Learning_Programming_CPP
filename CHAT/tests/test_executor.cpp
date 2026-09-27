#include <gtest/gtest.h>

#include <string>
#include <utility>

#include "executor.hpp"
#include "parser.hpp"


namespace {
    Response run(ChatStore& store, std::string text) {
        const InputLine line{std::move(text)};
        const auto command = parse(line);
        if (!command) {
            return Response{false, "(not parsed)"};
        }
        return execute(store, *command);
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

TEST(Executor, HistoryOfMissingRoomIsNotOk) {
    ChatStore store;
    EXPECT_FALSE(run(store, "/history #absent").ok);
}

TEST(Executor, HistoryRespectsLimit) {
    ChatStore store;
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
    run(store, "/msg #general text");
    EXPECT_EQ(run(store, "/type #general 0").text, std::string("text"));
}

TEST(Executor, ReactOnTextFailsWithoutThrowing) {
    ChatStore store;
    run(store, "/msg #general text");

    EXPECT_FALSE(run(store, "/react #general 0").ok);
}

TEST(Executor, ReactOnReactionAccumulates) {
    ChatStore store;
    store.post("#general", Message("me", Body{Reaction{"+1", 0}}));
    EXPECT_EQ(run(store, "/react #general 0").text, std::string("1"));
    EXPECT_EQ(run(store, "/react #general 0 4").text, std::string("5"));
}

TEST(Executor, MissingMessageIndexIsNotOk) {
    ChatStore store;
    run(store, "/msg #general text");
    EXPECT_FALSE(run(store, "/type #general 99").ok);
}
