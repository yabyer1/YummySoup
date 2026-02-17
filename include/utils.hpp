#include <string_view>
#ifndef UTILS_HPP
#define UTILS_HPP

#include <errno.h>

 inline void checked_write(int fd, const void* buf, size_t count) {
    size_t total_written = 0;
    const char* ptr = static_cast<const char*>(buf);

    while (total_written < count) {
        ssize_t n = write(fd, ptr + total_written, count - total_written);

        if (n > 0) {
            total_written += n;
        } else if (n == -1) {
            // Handle transient "buffer full" errors by retrying
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue; 
            }
            // If it's a real error (like a broken pipe), fail fast
            serverAssert(false && "Critical socket write failure");
        }
    }
}
// this isnt regex this is globbing 
// ? matches any single character so h?l will amtch hel and hal but not hello
// * matches any sequence of characters (including the empty sequence) so h*o will match ho, hheijfowejeowio, but not hll, sequenc emust still end in o and start wiht h
bool stringmatchlen(std::string_view pattern, std::string_view str) {
    size_t p = 0; // Pattern pointer
    size_t s = 0; // String pointer
    size_t star_idx = std::string_view::npos; 
    size_t match_idx = 0; 

    while (s < str.size()) {
        // 1. If characters match or we hit '?', just move forward
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == str[s])) {
            p++;
            s++;
        }
        // 2. If we hit a '*', remember this spot and assume it matches 0 chars for now
        else if (p < pattern.size() && pattern[p] == '*') {
            star_idx = p;
            match_idx = s;
            p++; // Move pattern to the char AFTER the '*'
        }
        // 3. Mismatch! If we saw a '*' earlier, backtrack.
        // This is your "j++" logic: we assume the '*' matches one more char than before.
        else if (star_idx != std::string_view::npos) {
            p = star_idx + 1; // Go back to the char after the '*'
            match_idx++;      // Increment the "skip" count in the input string
            s = match_idx;    // Start matching from the new shifted position
        }
        // 4. No match and no '*' to save us
        else {
            return false;
        }
    }

    // 5. If pattern has trailing stars, they match empty space at the end of str
    while (p < pattern.size() && pattern[p] == '*') {
        p++;
    }

    return p == pattern.size() && s == str.size(); // Both pattern and string should be fully matched
}

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



#endif