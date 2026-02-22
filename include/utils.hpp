class Client;
#ifndef UTILS_HPP
#define UTILS_HPP

#include <memory>
#include <string_view>
#include <vector>

// Forward declaration: Tell the compiler Client exists
class Client; 

// Only DECLARATIONS (no curly braces with logic)
void checked_write(const std::shared_ptr<Client>& c, const void* buf, size_t count);
void checked_write(Client* c, const void* buf, size_t count);
void checked_write_fd(int fd, const void* buf, size_t count);

bool stringmatchlen(std::string_view pattern, std::string_view str);

#endif

#ifdef TESTING_MODE
void test_stringmatchlen() {
    std::cout << "Running Stringmatch Tests..." << std::endl;

    // Standard matches
    serverAssert(stringmatchlen("h?llo", "hello") == true);
    serverAssert(stringmatchlen("h*o", "hello") == true);
    serverAssert(stringmatchlen("h*o", "hallo") == true);
    
    // Multiple wildcards
    serverAssert(stringmatchlen("*world*", "hello world again") == true);
    serverAssert(stringmatchlen("a*b*c", "abbbbbc") == true);
    
    // Non-matches
    serverAssert(stringmatchlen("h?llo", "heello") == false);
    serverAssert(stringmatchlen("h*o", "hellp") == false);
    
    // Edge cases
    serverAssert(stringmatchlen("*", "anything") == true);
    serverAssert(stringmatchlen("", "") == true);
    serverAssert(stringmatchlen("a", "") == false);

    std::cout << "\033[1;32m✓ Stringmatch Tests Passed!\033[0m" << std::endl;
}
#endif
