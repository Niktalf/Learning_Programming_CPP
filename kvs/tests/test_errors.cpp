#include <gtest/gtest.h>

#include <exception>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "kvs/errors.hpp"

using kvs::KeyNotFoundError;
using kvs::KvError;
using kvs::ParseError;
using kvs::TypeMismatchError;

TEST(Errors, HierarchyIsRootedAtKvError) {
    // Одна ветка catch (const KvError&) ловит любую ошибку проекта, а
    // catch (const std::runtime_error&) — любую ошибку вообще.
    EXPECT_TRUE((std::is_base_of_v<std::runtime_error, KvError>));
    EXPECT_TRUE((std::is_base_of_v<KvError, ParseError>));
    EXPECT_TRUE((std::is_base_of_v<KvError, KeyNotFoundError>));
    EXPECT_TRUE((std::is_base_of_v<KvError, TypeMismatchError>));
}

TEST(Errors, ContextIsInFieldsNotOnlyInMessage) {
    const TypeMismatchError e("counter", "int", "string");
    // На этапе 16 код ответа выбирается по полям, а не по разбору what().
    EXPECT_EQ(e.key(), std::string("counter"));
    EXPECT_EQ(e.expected(), std::string("int"));
    EXPECT_EQ(e.actual(), std::string("string"));
}

TEST(Errors, CatchingByBaseReferenceKeepsDerivedType) {
    try {
        throw KeyNotFoundError("absent");
    } catch (const KvError& e) {
        // Поймали по ссылке на базу — динамический тип сохранился.
        const auto* derived = dynamic_cast<const KeyNotFoundError*>(&e);
        ASSERT_NE(derived, nullptr);
        EXPECT_EQ(derived->key(), std::string("absent"));
    }
}

TEST(Errors, MessageMentionsContext) {
    const ParseError e("unknown command: FOO", "FOO bar");
    const std::string what = e.what();
    EXPECT_NE(what.find("unknown command"), std::string::npos);
    EXPECT_NE(what.find("FOO bar"), std::string::npos);
}

TEST(Errors, StorageErrorsArePartOfHierarchy) {
    EXPECT_TRUE((std::is_base_of_v<KvError, kvs::OutOfMemoryError>));
    EXPECT_TRUE((std::is_base_of_v<KvError, kvs::OverflowError>));

    const kvs::OutOfMemoryError oom("big", 120, 100);
    EXPECT_EQ(oom.key(), std::string("big"));
    EXPECT_EQ(oom.required(), 120u);
    EXPECT_EQ(oom.limit(), 100u);
}

// Звёздочка: цепочка из двух звеньев разворачивается в причину и контекст.
TEST(Errors, NestedChainUnwindsToRootCause) {
    std::exception_ptr chain;
    try {
        try {
            throw KeyNotFoundError("absent");
        } catch (...) {
            std::throw_with_nested(KvError("while executing GET absent"));
        }
    } catch (...) {
        chain = std::current_exception();
    }

    try {
        std::rethrow_exception(kvs::root_cause(chain));
    } catch (const KeyNotFoundError& e) {
        EXPECT_EQ(e.key(), std::string("absent"));
    } catch (...) {
        ADD_FAILURE() << "root cause has the wrong type";
    }

    const std::vector<std::string> context = kvs::context_of(chain);
    ASSERT_EQ(context.size(), 1u);
    EXPECT_EQ(context.front(), std::string("while executing GET absent"));
}

TEST(Errors, PlainExceptionIsItsOwnRootCause) {
    const std::exception_ptr single = std::make_exception_ptr(ParseError("empty line", ""));
    EXPECT_EQ(kvs::root_cause(single), single);
    EXPECT_TRUE(kvs::context_of(single).empty());
}
