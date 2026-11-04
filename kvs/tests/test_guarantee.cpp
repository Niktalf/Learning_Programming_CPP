#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "kvs/errors.hpp"
#include "kvs/ivalue.hpp"
#include "kvs/store.hpp"
#include "kvs/value.hpp"

namespace {

// Тип, бросающий на N-й операции копирования. Без него сильную гарантию
// доказать нечем: рассуждения на словах доказательством не являются.
int& countdown() {
    static int n = -1;
    return n;
}

class ThrowOnNthCopy {
public:
    ThrowOnNthCopy() = default;
    explicit ThrowOnNthCopy(std::string text) : text_(std::move(text)) {}

    ThrowOnNthCopy(const ThrowOnNthCopy& other) : text_(other.text_) { tick(); }
    ThrowOnNthCopy& operator=(const ThrowOnNthCopy& other) {
        tick();
        text_ = other.text_;
        return *this;
    }
    // Перемещение не бросает — ровно как у настоящих типов проекта.
    ThrowOnNthCopy(ThrowOnNthCopy&&) noexcept = default;
    ThrowOnNthCopy& operator=(ThrowOnNthCopy&&) noexcept = default;

    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    [[nodiscard]] std::size_t size() const noexcept { return text_.size(); }

private:
    static void tick() {
        if (countdown() > 0 && --countdown() == 0) {
            throw std::bad_alloc();
        }
    }
    std::string text_;
};

using Store = kvs::BasicStore<ThrowOnNthCopy>;
using Pairs = std::vector<std::pair<std::string, ThrowOnNthCopy>>;

}  // namespace

TEST(Guarantee, MsetIsAllOrNothing) {
    const Pairs pairs = {{"a", ThrowOnNthCopy("new-a")},
                         {"c", ThrowOnNthCopy("new-c")},
                         {"d", ThrowOnNthCopy("new-d")},
                         {"e", ThrowOnNthCopy("new-e")}};

    // Успешный MSET делает 6 копирований: 2 на копию таблицы и по одному на
    // каждую из четырёх пар. Бросок перебирается на каждом из них — и в копии
    // таблицы, и на первой паре, и в середине, и на последней. Одна точка
    // броска доказывала бы гарантию только для неё.
    constexpr int copies_in_successful_mset = 6;
    for (int nth = 1; nth <= copies_in_successful_mset; ++nth) {
        Store store;
        store.set("a", ThrowOnNthCopy("old-a"));
        store.set("b", ThrowOnNthCopy("old-b"));

        countdown() = nth;
        bool thrown = false;
        try {
            store.mset(pairs);
        } catch (const std::bad_alloc&) {
            thrown = true;
        }
        countdown() = -1;

        ASSERT_TRUE(thrown) << "copy " << nth;
        // Хранилище не изменилось: ни одна новая пара не записана, старые целы.
        EXPECT_EQ(store.size(), 2u) << "copy " << nth;
        EXPECT_EQ(store.get("a")->text(), std::string("old-a")) << "copy " << nth;
        EXPECT_EQ(store.get("b")->text(), std::string("old-b")) << "copy " << nth;
        EXPECT_FALSE(store.contains("c")) << "copy " << nth;
        EXPECT_FALSE(store.contains("e")) << "copy " << nth;
    }

    // Седьмого копирования нет: перебор выше покрыл все точки броска.
    Store store;
    store.set("a", ThrowOnNthCopy("old-a"));
    store.set("b", ThrowOnNthCopy("old-b"));
    countdown() = copies_in_successful_mset + 1;
    EXPECT_NO_THROW(store.mset(pairs));
    countdown() = -1;
}

TEST(Guarantee, MsetWithoutFailureWritesEverything) {
    Store store;
    store.set("a", ThrowOnNthCopy("old-a"));

    const Pairs pairs = {{"a", ThrowOnNthCopy("new-a")}, {"b", ThrowOnNthCopy("new-b")}};
    store.mset(pairs);

    EXPECT_EQ(store.size(), 2u);
    EXPECT_EQ(store.get("a")->text(), std::string("new-a"));
    EXPECT_EQ(store.get("b")->text(), std::string("new-b"));
}

TEST(Guarantee, SwapIsNoexcept) {
    // Без noexcept на swap последняя операция mset перестала бы быть точкой
    // невозврата, и сильная гарантия выродилась бы в базовую.
    Store a;
    Store b;
    EXPECT_TRUE(noexcept(swap(a, b)));
}

TEST(Guarantee, MsetOverMemoryLimitChangesNothing) {
    // Предел 20 байт: «a»+«old-a» и «b»+«old-b» занимают 12. Третья пара MSET
    // его превысит — бросок посреди операции, но уже не при копировании,
    // а по решению самого хранилища.
    Store store(20);
    store.set("a", ThrowOnNthCopy("old-a"));
    store.set("b", ThrowOnNthCopy("old-b"));
    const std::size_t used_before = store.used_bytes();

    const Pairs pairs = {{"a", ThrowOnNthCopy("x")},
                         {"c", ThrowOnNthCopy("y")},
                         {"d", ThrowOnNthCopy("long-enough-to-overflow")}};

    EXPECT_THROW(store.mset(pairs), kvs::OutOfMemoryError);
    EXPECT_EQ(store.size(), 2u);
    EXPECT_EQ(store.get("a")->text(), std::string("old-a"));
    EXPECT_FALSE(store.contains("c"));
    EXPECT_EQ(store.used_bytes(), used_before);   // учёт памяти тоже не тронут
}

TEST(Guarantee, SetOverMemoryLimitChangesNothing) {
    Store store(10);
    store.set("k", ThrowOnNthCopy("12345"));
    try {
        store.set("k", ThrowOnNthCopy("123456789012"));
        ADD_FAILURE() << "limit must be enforced";
    } catch (const kvs::OutOfMemoryError& e) {
        EXPECT_EQ(e.key(), std::string("k"));
        EXPECT_EQ(e.required(), 13u);
        EXPECT_EQ(e.limit(), 10u);
    }
    EXPECT_EQ(store.get("k")->text(), std::string("12345"));
    EXPECT_EQ(store.used_bytes(), 6u);
}

TEST(Guarantee, DeleteReleasesMemory) {
    Store store(10);
    store.set("k", ThrowOnNthCopy("12345678"));
    EXPECT_TRUE(store.del("k"));
    EXPECT_EQ(store.used_bytes(), 0u);
    store.set("m", ThrowOnNthCopy("12345678"));   // место освободилось
    EXPECT_TRUE(store.contains("m"));
}

// --- Этап 4: та же гарантия на настоящих полиморфных значениях -------------
//
// После перехода на unique_ptr копирование хранилища держится на clone. Если
// бы Value копировался поверхностно — или не копировался вовсе, — MSET
// потерял бы сильную гарантию. Тест подставляет значение, чей clone бросает
// на N-м вызове, и повторяет перебор точек броска.

namespace {

int& clone_countdown() {
    static int n = -1;
    return n;
}

class ThrowingClone final : public kvs::IValue {
public:
    explicit ThrowingClone(std::string text) : text_(std::move(text)) {}
    ThrowingClone(const ThrowingClone&) = default;
    ThrowingClone& operator=(const ThrowingClone&) = delete;

    [[nodiscard]] std::string_view type_name() const noexcept override { return "throwing"; }
    [[nodiscard]] std::string to_string() const override { return text_; }
    [[nodiscard]] std::size_t size() const noexcept override { return text_.size(); }
    [[nodiscard]] std::unique_ptr<kvs::IValue> clone() const override {
        if (clone_countdown() > 0 && --clone_countdown() == 0) {
            throw std::bad_alloc();
        }
        return std::make_unique<ThrowingClone>(*this);
    }
    [[nodiscard]] bool equals(const kvs::IValue& other) const noexcept override {
        const auto* const same = dynamic_cast<const ThrowingClone*>(&other);
        return same != nullptr && same->text_ == text_;
    }

private:
    std::string text_;
};

kvs::Value throwing(std::string text) {
    return kvs::Value(std::make_unique<ThrowingClone>(std::move(text)));
}

}  // namespace

TEST(Guarantee, MsetWithPolymorphicValues) {
    std::vector<std::pair<std::string, kvs::Value>> pairs;
    pairs.emplace_back("a", throwing("new-a"));
    pairs.emplace_back("c", throwing("new-c"));
    pairs.emplace_back("d", throwing("new-d"));

    // Два clone на копию таблицы и по одному на каждую из трёх пар.
    constexpr int clones_in_successful_mset = 5;
    for (int nth = 1; nth <= clones_in_successful_mset; ++nth) {
        kvs::Store store;
        store.set("a", throwing("old-a"));
        store.set("b", throwing("old-b"));

        clone_countdown() = nth;
        EXPECT_THROW(store.mset(pairs), std::bad_alloc) << "clone " << nth;
        clone_countdown() = -1;

        EXPECT_EQ(store.size(), 2u) << "clone " << nth;
        EXPECT_EQ(store.get("a")->to_string(), std::string("old-a")) << "clone " << nth;
        EXPECT_FALSE(store.contains("c")) << "clone " << nth;
    }
}
