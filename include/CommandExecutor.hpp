#ifndef COMMAND_EXECUTOR_HPP
#define COMMAND_EXECUTOR_HPP

#include <unordered_map>
#include <string_view>
#include <functional>
#include <vector>
#include <memory>
#include "Client.hpp"
#include "Storage.hpp"

// Function signature for all commands
using CommandFunc = std::function<void(std::shared_ptr<Client>, const std::vector<std::string_view>&, Storage&)>;

class CommandExecutor {
private:
    std::unordered_map<std::string_view, CommandFunc> commands;

public:
    CommandExecutor() {
        // Define SET
        commands["SET"] = [](auto client, const auto& args, Storage& db) {
            if (args.size() < 3) {
                std::string err = "-ERR wrong number of arguments for 'set'\r\n";
                write(client->fd, err.c_str(), err.length());
                return;
            }
            db.set(std::string(args[1]), std::string(args[2]));
            write(client->fd, "+OK\r\n", 5);
        };

        // Define GET
        commands["GET"] = [](auto client, const auto& args, Storage& db) {
            if (args.size() < 2) {
                std::string err = "-ERR wrong number of arguments for 'get'\r\n";
                write(client->fd, err.c_str(), err.length());
                return;
            }
            std::string val = db.get(std::string(args[1]));
            if (val == "(nil)") {
                write(client->fd, "$-1\r\n", 5); // Redis-style null
            } else {
                std::string resp = "$" + std::to_string(val.length()) + "\r\n" + val + "\r\n";
                write(client->fd, resp.c_str(), resp.length());
            }
        };

        // Define PING
        commands["PING"] = [](auto client, const auto& args, Storage& db) {
            write(client->fd, "+PONG\r\n", 7);
        };
    }

    void execute(std::shared_ptr<Client> client, const std::vector<std::string_view>& args, Storage& db) {
        if (args.empty()) return;
        
        auto it = commands.find(args[0]); // args[0] is the command name (e.g., "SET")
        if (it != commands.end()) {
            it->second(client, args, db);
        } else {
            std::string err = "-ERR unknown command '" + std::string(args[0]) + "'\r\n";
            write(client->fd, err.c_str(), err.length());
        }
    }
};

#endif