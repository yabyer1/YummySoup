#ifndef COMMAND_EXECUTOR_HPP
#define COMMAND_EXECUTOR_HPP

#include <unordered_map>
#include <string_view>
#include <functional>
#include <vector>
#include <memory>
#include "Client.hpp"
#include "Storage.hpp"
#include "ServerContext.hpp"
// Function signature for all commands
using CommandFunc = std::function<void(std::shared_ptr<Client>, const std::vector<std::string_view>&, Storage&)>;

class CommandExecutor {
private:
    std::unordered_map<std::string_view, CommandFunc> commands;
   


public:
  CommandExecutor() {
        // --- SET Command ---
        commands["SET"] = [this](auto client, const auto& args, Storage& db) {
            if (args.size() < 3) {
                (void)write(client->fd, "-ERR wrong number of arguments for 'set'\r\n", 42);
                return;
            }
            db.set(std::string(args[1]), std::string(args[2]));
            
            // Log to AOF for durability
            ctx.aof.log(args); 
            
            (void)write(client->fd, "+OK\r\n", 5);
        };

        // --- GET Command ---
        commands["GET"] = [](auto client, const auto& args, Storage& db) {
            if (args.size() < 2) {
                (void)write(client->fd, "-ERR wrong number of arguments for 'get'\r\n", 42);
                return;
            }
            std::string val = db.get(std::string(args[1]));
            if (val == "(nil)") {
                (void)write(client->fd, "$-1\r\n", 5);
            } else {
                std::string resp = "$" + std::to_string(val.length()) + "\r\n" + val + "\r\n";
                (void)write(client->fd, resp.c_str(), resp.length());
            }
        };

        // --- DEL Command ---
        commands["DEL"] = [this](auto client, const auto& args, Storage& db) {
            if (args.size() < 2) {
                (void)write(client->fd, "-ERR wrong number of arguments for 'del'\r\n", 42);
                return;
            }
            db.del(std::string(args[1]));
            ctx.aof.log(args);
            (void)write(client->fd, "+OK\r\n", 5);
        };
        // --- PING Command ---
        commands["PING"] = [](auto client, const auto& args, Storage& db) {
            (void)write(client->fd, "+PONG\r\n", 7);
        };
    }

    void CommandExecutor::execute(std::shared_ptr<Client> client, 
                             const std::vector<std::string_view>& tokens, 
                             Storage& db) {
  if(tokens.empty()) return;
  //convert the command name to uppercase for case-insensitive matching
  auto it = commands.find(tokens[0]);
  if(it != commands.end()){
    it ->second(client, tokens, db); //execute the command function
  }
  else{
    std::string err = "-ERR unknown command '" + std::string(tokens[0]) + "'\r\n";
    (void)write(client->fd, "-ERR unknown command\r\n", 22);
  }
}
};

#endif