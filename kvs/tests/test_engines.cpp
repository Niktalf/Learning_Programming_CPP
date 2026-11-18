// Этап 8: каждый движок за одним интерфейсом — один и тот же набор проверок.
// Параметризованный тест: тело одно, движков четыре.

#include <gtest/gtest.h>

#include <memory>
#include <set>
#include <string>
#include <string_view>

#include "kvs/engines.hpp"
#include "kvs/store.hpp"

using kvs::EngineKind;

namespace {

struct Entry {
    int value = 0;
};

class Engines : public ::testing::TestWithParam<EngineKind> {
protected:
    std::unique_ptr<kvs::IStorageEngine<Entry>> engine = kvs::make_engine<Entry>(GetParam());
};

}  // namespace

TEST_P(Engines, PutFindEraseSize) {
    EXPECT_TRUE(engine->put("a", Entry{1}));
    EXPECT_TRUE(engine->put("b", Entry{2}));
    EXPECT_FALSE(engine->put("a", Entry{3}));   // замена
    EXPECT_EQ(engine->size(), 2u);
    EXPECT_EQ(engine->find("a")->value, 3);
    EXPECT_TRUE(engine->contains("b"));
    EXPECT_TRUE(engine->erase("a"));
    EXPECT_FALSE(engine->erase("a"));
    EXPECT_EQ(engine->find("a"), nullptr);
    EXPECT_EQ(engine->size(), 1u);
}

TEST_P(Engines, TraversalVisitsEveryKeyOnceAcrossGrowth) {
    for (int i = 0; i < 5000; ++i) {
        engine->put("key:" + std::to_string(i), Entry{i});
    }
    for (int i = 0; i < 5000; i += 3) {
        engine->erase("key:" + std::to_string(i));
    }
    std::multiset<std::string> seen;
    for (auto slot = engine->first_from(0); slot; slot = engine->next(*slot)) {
        seen.insert(engine->key_at(*slot));
        EXPECT_EQ("key:" + std::to_string(engine->entry_at(*slot).value), engine->key_at(*slot));
    }
    EXPECT_EQ(seen.size(), engine->size());
    EXPECT_EQ(std::set<std::string>(seen.begin(), seen.end()).size(), seen.size());   // без повторов
}

TEST_P(Engines, CloneIsIndependent) {
    engine->put("a", Entry{1});
    const auto copy = engine->clone();
    engine->put("a", Entry{2});
    engine->put("b", Entry{3});
    EXPECT_EQ(copy->find("a")->value, 1);
    EXPECT_EQ(copy->size(), 1u);
    EXPECT_EQ(copy->name(), engine->name());
}

TEST_P(Engines, NameMatchesKind) {
    EXPECT_EQ(engine->name(), kvs::name_of(GetParam()));
    EXPECT_EQ(kvs::parse_engine_kind(engine->name()), GetParam());
}

TEST_P(Engines, StoreChoosesItsEngine) {
    const kvs::Store store(GetParam());
    EXPECT_EQ(store.engine_name(), kvs::name_of(GetParam()));
}

INSTANTIATE_TEST_SUITE_P(AllEngines, Engines,
                         ::testing::Values(EngineKind::Std, EngineKind::Hash, EngineKind::BadHash,
                                           EngineKind::Open),
                         [](const auto& test) {
                             std::string name(kvs::name_of(test.param));
                             std::erase(name, '-');
                             return name;
                         });

TEST(EngineKind, UnknownNameIsRejected) {
    EXPECT_FALSE(kvs::parse_engine_kind("btree").has_value());
}
