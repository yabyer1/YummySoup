#ifndef PROTOCOL_HANDLER_HPP
#define PROTOCOL_HANDLER_HPP
#include "common.hpp"
#include <vector>
#include <string_view>
#include <optional>
#include <cstring>

class ProtocolHandler {
public:
    // We pass the raw buffer and a reference to the current index (how much data is in it)
    static std::optional<std::vector<std::string_view>> parse(char* buffer, int& buffer_index) {
        serverAssert(buffer_index <= 4096);
        // Wrap raw memory in a view for easy searching without copying
        std::string_view data_view(buffer, buffer_index);
        
        size_t pos = data_view.find('\n');
        if (pos == std::string_view::npos) return std::nullopt;

        std::string_view line = data_view.substr(0, pos);
        
        // Trim \r if it's there
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }

        std::vector<std::string_view> tokens;
        size_t start = 0, end = 0;
        while ((end = line.find(' ', start)) != std::string_view::npos) { //this is pretty much java string.split() on a space
            if (end > start) tokens.push_back(line.substr(start, end - start));
            start = end + 1;
        }
        if (start < line.length()) tokens.push_back(line.substr(start));

        // CRITICAL: "Consume" the data by shifting the remaining bytes left after this line 
        size_t bytes_consumed = pos + 1;
        serverAssert(bytes_consumed <= (size_t)buffer_index);
        size_t remaining = buffer_index - bytes_consumed;
        
        if (remaining > 0) {
            // Use memmove because memory regions overlap!
            memmove(buffer, buffer + bytes_consumed, remaining); // say we have consumed 8 bytes of data and we have  3 remaining bytes that came after the \n so now right now our buffer_index is pointing to 11 (current free spot) we call memove(buffer, buffer + 8, 3) and the cpu will copy the values from index 8 - 0, 9 - 1 , 10 - 2, this will allow for the remaining 3 bytes to now sit at buffer 0, 1 , 2 and ourpointer is at 3. we dont care about the rest of the data as we have what we need at our current spot. this is inefficeint tho
        }
        buffer_index = remaining; // Update the client's write head
        
        return tokens; //return the tokens for this line back
    }
    static std::string to_resp(const std::vector<std::string_view>& tokens) {
        //format as *<num_args>\r\n$<arg1_len>\r\n<arg1>\r\n//////
       std::string resp = "*";
       resp += std::to_string(tokens.size()) + "\r\n";
         for (const auto& token : tokens) {
                resp += "$" + std::to_string(token.size()) + "\r\n" + std::string(token) + "\r\n";
         }
        return resp;
    }
    static std::string format_pubsub(std::string_view type, std::string_view channel, std::string_view message) {
    // Pub/Sub messages are RESP Arrays of 3 elements: ["message", channel, data]
    std::vector<std::string_view> tokens = { type, channel, message };
    return to_resp(tokens);
}
};

#endif
#ifdef TESTING_MODE
void test_protocol_handler() {
    std::cout << "Running ProtocolHandler Tests..." << std::endl;
    
    char buffer[] = "*2\r\n$3\r\nGET\r\n$4\r\nname\r\n";
    int len = strlen(buffer);
    auto tokens = ProtocolHandler::parse(buffer, len);
    
    serverAssert(tokens.has_value());
    serverAssert(tokens->size() == 2);
    serverAssert((*tokens)[0] == "GET");
    
    std::cout << "ProtocolHandler Tests Passed!" << std::endl;
}

void test_buffer_sliding() {
    std::cout << "Testing Buffer Sliding (memmove)..." << std::endl;

    char buffer[4096];
    // Simulate two commands in one buffer
    strcpy(buffer, "SET a 1\nGET a\n");
    int buffer_index = strlen(buffer);

    // Parse first command
    auto tokens1 = ProtocolHandler::parse(buffer, buffer_index);
    serverAssert(tokens1->size() == 3);
    serverAssert((*tokens1)[0] == "SET");
    
    // Check that buffer_index was reduced and "GET a\n" moved to front
    serverAssert(buffer_index == 6); 
    serverAssert(strncmp(buffer, "GET a\n", 6) == 0);

    // Parse second command
    auto tokens2 = ProtocolHandler::parse(buffer, buffer_index);
    serverAssert(tokens2->size() == 2);
    serverAssert((*tokens2)[0] == "GET");
    
    // Final check: buffer should be empty
    serverAssert(buffer_index == 0);

    std::cout << "\033[1;32m✓ Buffer Sliding Tests Passed!\033[0m" << std::endl;
}

#endif