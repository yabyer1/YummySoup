#ifndef COMMON_HPP
#define COMMON_HPP

#include <iostream>
#include <cstdlib>
#include <string_view>
#include "utils.hpp"
// Visa-grade Assertion Macro
#define serverAssert(expr) do { \
    if (!(expr)) { \
        std::cerr << "\n\033[1;31m[ASSERTION FAILED]\033[0m " << #expr << std::endl; \
        std::cerr << "Location: " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

 
#endif