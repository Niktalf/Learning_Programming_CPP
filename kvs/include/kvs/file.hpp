#pragma once

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace kvs {

// Обёртка над ресурсом из C: FILE* из std::fopen закрывается std::fclose.
// unique_ptr умеет вызывать вместо delete любой делетер.
//
// Делетер — тип без состояния. Это существенно для размера: unique_ptr
// хранит делетер рядом с указателем, и пустой класс благодаря оптимизации
// пустой базы места не занимает. Итог — unique_ptr ровно того же размера,
// что и сырой FILE*. Делетер-указатель на функцию — это данные, его пришлось
// бы хранить, и размер удвоился бы. Сравнение трёх видов делетера — в тесте
// File.DeleterSize.
struct FileCloser {
    void operator()(std::FILE* file) const noexcept {
        if (file != nullptr) {
            std::fclose(file);
        }
    }
};

using FilePtr = std::unique_ptr<std::FILE, FileCloser>;

// Открывает файл или бросает KvError.
[[nodiscard]] FilePtr open_file(const std::filesystem::path& path, const char* mode);

// Читает файл построчно — через FILE*, чтобы делетер работал на настоящем
// чтении, а не только в тесте на размер. Им читается журнал при запуске.
[[nodiscard]] std::vector<std::string> read_lines(const std::filesystem::path& path);

}  // namespace kvs
