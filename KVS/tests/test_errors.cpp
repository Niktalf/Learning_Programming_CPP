#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <type_traits>

#include "errors.hpp"

TEST(Errors, HierarchyIsRootedAtKvError) {
    EXPECT_TRUE((std::is_base_of_v<std::runtime_error, KvError>));
    EXPECT_TRUE((std::is_base_of_v<KvError, ParseError>));
    EXPECT_TRUE((std::is_base_of_v<KvError, KeyNotFoundError>));
    EXPECT_TRUE((std::is_base_of_v<KvError, TypeMismatchError>));
}

TEST(Errors, ContextIsInFieldsNotOnlyInMessage) {
    const TypeMismatchError e("counter", "int", "string");
    EXPECT_EQ(e.key(), std::string("counter"));
    EXPECT_EQ(e.expected(), std::string("int"));
    EXPECT_EQ(e.actual(), std::string("string"));
}

TEST(Errors, CatchingByBaseReferenceKeepsDerivedType) {
    try {
        throw KeyNotFoundError("absent");
    } catch (const KvError& e) {
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
