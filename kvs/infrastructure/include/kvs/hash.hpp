#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kvs {

// Этап 8: собственные хеш-функции для HashEngine. std::hash здесь не
// используется — в этом смысл задания.
//
// FNV-1a, 64 бита. Для каждого байта: сначала XOR с байтом, затем умножение
// на простое число FNV. Порядок «сначала XOR» — это «1a»; у FNV-1 наоборот,
// и последний байт у него перемешивается хуже. Константы — из описания
// алгоритма (Fowler, Noll, Vo): начальное значение 14695981039346656037 и
// простое 1099511628211. Умножение на нечётное число по модулю 2^64
// обратимо, поэтому разные состояния не сливаются, а XOR вносит байт так,
// что разные байты на одной позиции дают разные состояния.
//
// Затравка (seed) смешивается с начальным значением. Она нужна против
// атаки на хеш-таблицу (docs/benchmarks.md этапа 7): без неё ключи с
// одинаковым хешем можно подобрать заранее. С затравкой, которую клиент не
// знает, подобрать их для FNV труднее, но не невозможно: FNV — не
// криптографическая функция, и для сервера в открытой сети правильный ответ
// — SipHash. Это записано в docs/hashing.md, а не замолчано.
inline constexpr std::uint64_t fnv_offset_basis = 14695981039346656037ULL;
inline constexpr std::uint64_t fnv_prime = 1099511628211ULL;

//
// Хеш — свёртка: каждый байт обновляет состояние. ranges::fold_left из C++23
// и constexpr, поэтому тестовые векторы ниже проверяются при компиляции.
[[nodiscard]] constexpr std::uint64_t fnv1a(std::string_view bytes, std::uint64_t seed = 0) noexcept {
    return std::ranges::fold_left(bytes, fnv_offset_basis ^ seed, [](std::uint64_t hash, char c) {
        return (hash ^ static_cast<unsigned char>(c)) * fnv_prime;
    });
}

// Заведомо плохая функция для контраста — сумма кодов символов. Анаграммы
// («user:12» и «user:21») и вообще все ключи с одинаковым набором символов
// совпадают, а сумма растёт медленно: ключи одной длины из цифр ложатся в
// узкий диапазон значений, то есть в немногие корзины. Деградация поиска —
// в docs/hashing.md.
[[nodiscard]] constexpr std::uint64_t char_sum(std::string_view bytes) noexcept {
    return std::ranges::fold_left(bytes, std::uint64_t{0},
                                  [](std::uint64_t sum, char c) { return sum + static_cast<unsigned char>(c); });
}

// Хеш-функции как объекты для таблиц. is_transparent — поиск по
// string_view без временной строки.
struct Fnv1aHash {
    std::uint64_t seed = 0;
    using is_transparent = void;
    [[nodiscard]] std::size_t operator()(std::string_view key) const noexcept {
        return static_cast<std::size_t>(fnv1a(key, seed));
    }
};

struct CharSumHash {
    using is_transparent = void;
    [[nodiscard]] std::size_t operator()(std::string_view key) const noexcept {
        return static_cast<std::size_t>(char_sum(key));
    }
};

// Тесты свойств, которые должны быть известны при компиляции. Значения FNV-1a
// для пустой строки и для «a» — из опубликованных тестовых векторов.
static_assert(fnv1a("") == fnv_offset_basis);
static_assert(fnv1a("a") == 0xaf63dc4c8601ec8cULL);
static_assert(char_sum("user:12") == char_sum("user:21"));

}  // namespace kvs
