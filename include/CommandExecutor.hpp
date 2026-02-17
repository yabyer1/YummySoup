#ifndef COMMAND_EXECUTOR_HPP
#define COMMAND_EXECUTOR_HPP
#include "common.hpp"
#include <unordered_map>
#include <string_view>
#include <functional>
#include <vector>
#include <memory>
#include "Client.hpp"
#include "Storage.hpp"
#include "ServerContext.hpp"
// Function signature for all commands
using CommandFunc = std::function<void(std::shared_ptr<Client>, const std::vector<std::string_view>&, ServerContext&)>;

class CommandExecutor {
private:
    std::unordered_map<std::string_view, CommandFunc> commands;
   


public:
  CommandExecutor() {
        // --- SET Command ---
        commands["SET"] = [this](auto client, const auto& args, ServerContext& ctx) {
             Storage& db = ctx.db; // Access the storage from the server context
            if (args.size() < 3) {
                (void)checked_write(client->fd, "-ERR wrong number of arguments for 'set'\r\n", 42);
                return;
            }
            db.set(std::string(args[1]), std::string(args[2]));
            
            // Log to AOF for durability
            ctx.aof.log(args); 
            
            (void)checked_write(client->fd, "+OK\r\n", 5);
        };

        // --- GET Command ---
        commands["GET"] = [](auto client, const auto& args, ServerContext& ctx) {
            Storage& db = ctx.db; // Access the storage from the server context
            if (args.size() < 2) {
                (void)checked_write(client->fd, "-ERR wrong number of arguments for 'get'\r\n", 42);
                return;
            }
            std::string val = db.get(std::string(args[1]));
            if (val == "(nil)") {
                (void)checked_write(client->fd, "$-1\r\n", 5);
            } else {
                std::string resp = "$" + std::to_string(val.length()) + "\r\n" + val + "\r\n";
                (void)checked_write(client->fd, resp.c_str(), resp.length());
            }
        };

        // --- DEL Command ---
        commands["DEL"] = [this](auto client, const auto& args, ServerContext& ctx) {
            Storage& db = ctx.db; // Access the storage from the server context
          
            if (args.size() < 2) {
                (void)checked_write(client->fd, "-ERR wrong number of arguments for 'del'\r\n", 42);
                return;
            }
            db.del(std::string(args[1]));
            ctx.aof.log(args);
            (void)checked_write(client->fd, "+OK\r\n", 5);
        };
        // --- PING Command ---
        commands["PING"] = [](auto client, const auto& args, ServerContext& ctx) {
          
            (void)checked_write(client->fd, "+PONG\r\n", 7);
        };

        commands["PUBLISH"] = [this](auto client, const auto& args, ServerContext& ctx) {
            publish(args, ctx);
        };
        commands["SUBSCRIBE"] = [this](auto client, const auto& args, ServerContext& ctx) {
            subscribe(client.get(), args, ctx);
        };
        commands["UNSUBSCRIBE"] = [this](auto client, const auto& args, ServerContext& ctx) {
            unsubscribe(client.get(), args, ctx);
        };
        commands["PSUBSCRIBE"] = [this](auto client, const auto& args, ServerContext& ctx) {
            psubscribe(client.get(), args, ctx);
        };
        commands["PUNSUBSCRIBE"] = [this](auto client, const auto& args, ServerContext& ctx) {
            punsubscribe(client.get(), args, ctx);
        };
    }
    void punsubscribe(Client* c, const std::vector<std::string_view>& tokens, ServerContext& ctx) {
    std::lock_guard<std::mutex> lock(ctx.pubsub_mutex);
    for (size_t i = 1; i < tokens.size(); ++i) {
        std::string pattern(tokens[i]);
        c->subscribed_patterns.erase(pattern); // Remove from client's local set
        auto it = ctx.pubsub_patterns.begin();
        while (it != ctx.pubsub_patterns.end()) {
            if (it->client == c && it->pattern == pattern) {
                it = ctx.pubsub_patterns.erase(it);
            } else {
                ++it;
            }
        }
        // Respond with unsubscription confirmation (Redis protocol)
        sendSubscriptionConfirmation(c, "punsubscribe", pattern, c->subscribed_patterns.size());
    }
}

   void psubscribe(Client* c, const std::vector<std::string_view>& tokens, ServerContext& ctx) {
    std::lock_guard<std::mutex> lock(ctx.pubsub_mutex);
    for (size_t i = 1; i < tokens.size(); ++i) {
        std::string pattern(tokens[i]);
        
        // Only add if not already subscribed
        if (c->subscribed_patterns.insert(pattern).second) {
            ctx.pubsub_patterns.push_back({c, pattern});
        }
        // Respond with subscription confirmation (Redis protocol)
        sendSubscriptionConfirmation(c, "psubscribe", pattern, c->subscribed_patterns.size());
    }
   }
    void unsubscribe(Client* c, const std::vector<std::string_view>& tokens, ServerContext& ctx) {
    std::lock_guard<std::mutex> lock(ctx.pubsub_mutex);
    for (size_t i = 1; i < tokens.size(); ++i) {
        std::string channel(tokens[i]);
        c->subscribed_channels.erase(channel); // Remove from client's local set
        auto it = ctx.pubsub_channels.find(channel);
        if (it != ctx.pubsub_channels.end()) {
            it->second.remove(c); // O(N) in the list of subscribers for that channel
            if (it->second.empty()) {
                ctx.pubsub_channels.erase(it);
            }
        }
        // Respond with unsubscription confirmation (Redis protocol)
        sendSubscriptionConfirmation(c, "unsubscribe", channel, c->subscribed_channels.size());
    }
}
    void sendPubsubMessage(Client* c, std::string_view channel, std::string_view message) {
    std::string resp = ProtocolHandler::format_pubsub("message", channel, message);
    (void)checked_write(c->fd, resp.c_str(), resp.size());
    }
    void sendPatternMessage(Client* c, std::string_view pattern, std::string_view channel, std::string_view message) {
    std::string resp = ProtocolHandler::format_pubsub("pmessage", channel, message);
    (void)checked_write(c->fd, resp.c_str(), resp.size());
    }
        void publish(const std::vector<std::string_view>& tokens, ServerContext& ctx) {
    serverAssert(tokens.size() >= 3); // PUBLISH <channel> <message>
    
    std::string_view channel = tokens[1];
    std::string_view message = tokens[2];
    std::unordered_set<Client*> visited;

    std::lock_guard<std::mutex> lock(ctx.pubsub_mutex);

    // 1. Exact Match Broadcast
    auto it = ctx.pubsub_channels.find(std::string(channel));
    if (it != ctx.pubsub_channels.end()) {
        for (Client* c : it->second) {
            serverAssert(c != nullptr);
            sendPubsubMessage(c, channel, message);
            visited.insert(c);
        }
    }

    // 2. Pattern Match Broadcast
    for (auto& pat : ctx.pubsub_patterns) {
        if (visited.find(pat.client) == visited.end()) {
            if (stringmatchlen(pat.pattern, channel)) {
                sendPatternMessage(pat.client, pat.pattern, channel, message);
                visited.insert(pat.client);
            }
        }
    }
}
void sendSubscriptionConfirmation(Client* c, std::string_view type, std::string_view channel, size_t subscription_count) {
   std::string count_str = std::to_string(subscription_count);
    std::vector<std::string_view> tokens = { type, channel, count_str };
    std::string resp = ProtocolHandler::to_resp(tokens);
    (void)checked_write(c->fd, resp.c_str(), resp.size());
}
void subscribe(Client* c, const std::vector<std::string_view>& tokens, ServerContext& ctx) {
    std::lock_guard<std::mutex> lock(ctx.pubsub_mutex);
    for (size_t i = 1; i < tokens.size(); ++i) {
        std::string channel(tokens[i]);
        
        // Only add if not already subscribed
        if (c->subscribed_channels.insert(channel).second) {
            ctx.pubsub_channels[channel].push_back(c);
        }
        // Respond with subscription confirmation (Redis protocol)
        sendSubscriptionConfirmation(c, "subscribe", channel, c->subscribed_channels.size());
    }
}
    void execute(std::shared_ptr<Client> client, const std::vector<std::string_view>& tokens, ServerContext& ctx) {
    if (tokens.empty()) return;

    // Convert command name to uppercase
    std::string cmd_name(tokens[0]);
    for (auto & c: cmd_name) c = toupper(c);

    auto it = commands.find(cmd_name);
    if (it != commands.end()) {
        it->second(client, tokens, ctx);
    } else {
        std::string err = "-ERR unknown command '" + cmd_name + "'\r\n";
        (void)checked_write(client->fd, err.c_str(), err.size());
    }
}
};

#endif