#ifndef PROTOCOL_HANDLER_HPP
#define PROTOCOL_HANDLER_HPP

#include <vector>
#include <string_view>
#include <optional>
#include <cstring>

class ProtocolHandler {
public:
    // We pass the raw buffer and a reference to the current index (how much data is in it)
    static std::optional<std::vector<std::string_view>> parse(char* buffer, int& buffer_index) {
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
        while ((end = line.find(' ', start)) != std::string_view::npos) {
            if (end > start) tokens.push_back(line.substr(start, end - start));
            start = end + 1;
        }
        if (start < line.length()) tokens.push_back(line.substr(start));

        // CRITICAL: "Consume" the data by shifting the remaining bytes left
        size_t bytes_consumed = pos + 1;
        size_t remaining = buffer_index - bytes_consumed;
        
        if (remaining > 0) {
            // Use memmove because memory regions overlap!
            memmove(buffer, buffer + bytes_consumed, remaining);
        }
        buffer_index = remaining; // Update the client's write head
        
        return tokens;
    }
};

#endif