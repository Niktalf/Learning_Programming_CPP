#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "lexer.hpp"

int main() {
    std::vector<std::string_view> tokens;

    {
        const std::string line = "msg #general long-enough-to-allocate-on-heap";
        tokens = tokenize(line);
    }

    std::cout << "tokens: " << tokens.size() << '\n';
    if (!tokens.empty()) {
        std::cout << "first token: " << tokens.front() << '\n';
    }
    return 0;
}