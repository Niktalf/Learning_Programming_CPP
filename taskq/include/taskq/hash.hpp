#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace taskq {

// Этап 8: собственные хеш-функции для индекса задач по номеру. std::hash
// здесь не используется — в этом смысл задания.
//
// Номер задачи — 64-битное число, и выдаются номера подряд: 1, 2, 3, ...
// Хеш — FNV-1a над восемью байтами номера. Для каждого байта: сначала XOR с
// байтом, затем умножение на простое число FNV (Fowler, Noll, Vo).
// Умножение на нечётное число по модулю 2^64 обратимо, поэтому разные
// номера не сливаются в один хеш раньше времени, а XOR вносит каждый байт.
//
// Почему не тождественная функция. std::hash<std::uint64_t> в libstdc++ —
// это само число, и на номерах подряд с маской по младшим битам она даёт
// идеальное распределение: по номеру на корзину. Но стоит номерам пойти с
// шагом — например, у каждого из 1024 генераторов свой остаток, — и
// тождественная функция с маской складывает их в немногие корзины.
// Гистограмма обоих случаев — docs/hashing.md.
//
// Затравка (seed) смешивается с начальным значением: номера задач
// предсказуемы, и без затравки номера с одинаковым хешем можно подобрать
// заранее. FNV — не криптографическая функция; для сервиса в открытой сети
// правильный ответ — SipHash (docs/hashing.md).
inline constexpr std::uint64_t fnv_offset_basis = 14695981039346656037ULL;
inline constexpr std::uint64_t fnv_prime = 1099511628211ULL;

// Байты — от младшего к старшему, независимо от порядка байтов машины:
// хеш одинаков на любой платформе. Этап 8: свёртка по массиву сдвигов —
// constexpr, поэтому значения проверяются при компиляции.
[[nodiscard]] constexpr std::uint64_t fnv1a(std::uint64_t value, std::uint64_t seed = 0) noexcept {
    constexpr std::array<int, 8> shifts = {0, 8, 16, 24, 32, 40, 48, 56};
    return std::ranges::fold_left(shifts, fnv_offset_basis ^ seed, [value](std::uint64_t hash, int shift) {
        return (hash ^ ((value >> shift) & 0xFF)) * fnv_prime;
    });
}

struct Fnv1aHash {
    std::uint64_t seed = 0;
    [[nodiscard]] constexpr std::size_t operator()(std::uint64_t id) const noexcept {
        return static_cast<std::size_t>(fnv1a(id, seed));
    }
};

// Заведомо плохая функция для контраста — остаток от деления на небольшое
// число. Сколько бы корзин ни было в таблице, занятыми окажутся не больше
// шестнадцати, и каждая цепочка — шестнадцатая часть всех задач.
inline constexpr std::uint64_t bad_hash_modulus = 16;

struct ModuloHash {
    [[nodiscard]] constexpr std::size_t operator()(std::uint64_t id) const noexcept {
        return static_cast<std::size_t>(id % bad_hash_modulus);
    }
};

// Тождественная функция — то, что делает std::hash<uint64_t> в libstdc++.
// Только для гистограммы: показать, почему её не выбрали.
struct IdentityHash {
    [[nodiscard]] constexpr std::size_t operator()(std::uint64_t id) const noexcept {
        return static_cast<std::size_t>(id);
    }
};

// Проверки при компиляции. Значение для нуля — FNV-1a восьми нулевых
// байтов, посчитанное независимо от этого кода.
static_assert(fnv1a(0) == 0xa8c7f832281a39c5ULL);
static_assert(fnv1a(1) != fnv1a(256));   // байты на разных позициях различаются
static_assert(ModuloHash{}(1) == ModuloHash{}(17));

}  // namespace taskq
