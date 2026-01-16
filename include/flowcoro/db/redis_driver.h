#pragma once

#include "connection_pool.h"

// Redis
#ifdef FLOWCORO_HAS_REDIS
#include <hiredis/hiredis.h>
#endif

#include <string>
#include <vector>
#include <chrono>
#include <regex>

namespace flowcoro::db {

// RedisRedis
enum class RedisDataType {
    NIL,
    STRING,
    INTEGER,
    ARRAY,
    ERROR,
    STATUS
};

// RedisRedis
struct RedisValue {
    RedisDataType type = RedisDataType::NIL;
    std::string string_val;
    int64_t int_val = 0;
    std::vector<RedisValue> array_val;

    RedisValue() = default;
    RedisValue(RedisDataType t) : type(t) {}
    RedisValue(const std::string& s) : type(RedisDataType::STRING), string_val(s) {}
    RedisValue(int64_t i) : type(RedisDataType::INTEGER), int_val(i) {}
    RedisValue(const std::vector<RedisValue>& arr) : type(RedisDataType::ARRAY), array_val(arr) {}

    // 
    bool is_nil() const { return type == RedisDataType::NIL; }
    bool is_string() const { return type == RedisDataType::STRING; }
    bool is_integer() const { return type == RedisDataType::INTEGER; }
    bool is_array() const { return type == RedisDataType::ARRAY; }
    bool is_error() const { return type == RedisDataType::ERROR; }

    std::string to_string() const {
        switch (type) {
            case RedisDataType::STRING:
            case RedisDataType::STATUS:
            case RedisDataType::ERROR:
                return string_val;
            case RedisDataType::INTEGER:
                return std::to_string(int_val);
            case RedisDataType::NIL:
                return "(nil)";
            case RedisDataType::ARRAY:
                return "[array with " + std::to_string(array_val.size()) + " elements]";
            default:
                return "";
        }
    }

    operator std::string() const { return to_string(); }
    operator int64_t() const { return int_val; }
    operator bool() const { return type != RedisDataType::NIL && type != RedisDataType::ERROR; }
};

// RedisRedis
struct RedisResult {
    bool success{false};
    std::string error;
    RedisValue value;

    RedisResult() = default;
    RedisResult(const RedisValue& val) : success(true), value(val) {}
    RedisResult(const std::string& err) : success(false), error(err) {}

    operator bool() const { return success; }
};

#ifdef FLOWCORO_HAS_REDIS

// Redis
class RedisConnection : public IConnection {
public:
    RedisConnection(redisContext* context) : context_(context) {}

    ~RedisConnection() override {
        close();
    }

    // Redis
    Task<RedisResult> execute_redis(const std::string& command) {
        co_return co_await execute_redis_internal(command);
    }

    Task<RedisResult> execute_redis(const std::vector<std::string>& args) {
        co_return co_await execute_redis_args(args);
    }

    // 
    Task<RedisResult> set(const std::string& key, const std::string& value) {
        co_return co_await execute_redis({"SET", key, value});
    }

    Task<RedisResult> set(const std::string& key, const std::string& value,
                         std::chrono::seconds ttl) {
        co_return co_await execute_redis({"SETEX", key, std::to_string(ttl.count()), value});
    }

    Task<RedisResult> get(const std::string& key) {
        co_return co_await execute_redis({"GET", key});
    }

    Task<RedisResult> del(const std::string& key) {
        co_return co_await execute_redis({"DEL", key});
    }

    Task<RedisResult> exists(const std::string& key) {
        co_return co_await execute_redis({"EXISTS", key});
    }

    // 
    Task<RedisResult> hset(const std::string& key, const std::string& field, const std::string& value) {
        co_return co_await execute_redis({"HSET", key, field, value});
    }

    Task<RedisResult> hget(const std::string& key, const std::string& field) {
        co_return co_await execute_redis({"HGET", key, field});
    }

    Task<RedisResult> hgetall(const std::string& key) {
        co_return co_await execute_redis({"HGETALL", key});
    }

    Task<RedisResult> hdel(const std::string& key, const std::string& field) {
        co_return co_await execute_redis({"HDEL", key, field});
    }

    // 
    Task<RedisResult> lpush(const std::string& key, const std::string& value) {
        co_return co_await execute_redis({"LPUSH", key, value});
    }

    Task<RedisResult> rpush(const std::string& key, const std::string& value) {
        co_return co_await execute_redis({"RPUSH", key, value});
    }

    Task<RedisResult> lpop(const std::string& key) {
        co_return co_await execute_redis({"LPOP", key});
    }

    Task<RedisResult> rpop(const std::string& key) {
        co_return co_await execute_redis({"RPOP", key});
    }

    Task<RedisResult> lrange(const std::string& key, int64_t start, int64_t stop) {
        co_return co_await execute_redis({"LRANGE", key, std::to_string(start), std::to_string(stop)});
    }

    // 
    Task<RedisResult> sadd(const std::string& key, const std::string& member) {
        co_return co_await execute_redis({"SADD", key, member});
    }

    Task<RedisResult> srem(const std::string& key, const std::string& member) {
        co_return co_await execute_redis({"SREM", key, member});
    }

    Task<RedisResult> smembers(const std::string& key) {
        co_return co_await execute_redis({"SMEMBERS", key});
    }

    Task<RedisResult> sismember(const std::string& key, const std::string& member) {
        co_return co_await execute_redis({"SISMEMBER", key, member});
    }

    // 
    Task<RedisResult> zadd(const std::string& key, double score, const std::string& member) {
        co_return co_await execute_redis({"ZADD", key, std::to_string(score), member});
    }

    Task<RedisResult> zrange(const std::string& key, int64_t start, int64_t stop) {
        co_return co_await execute_redis({"ZRANGE", key, std::to_string(start), std::to_string(stop)});
    }

    Task<RedisResult> zrem(const std::string& key, const std::string& member) {
        co_return co_await execute_redis({"ZREM", key, member});
    }

    // 
    Task<RedisResult> expire(const std::string& key, std::chrono::seconds ttl) {
        co_return co_await execute_redis({"EXPIRE", key, std::to_string(ttl.count())});
    }

    Task<RedisResult> ttl(const std::string& key) {
        co_return co_await execute_redis({"TTL", key});
    }

    // IConnection ()
    Task<QueryResult> execute(const std::string& sql) override {
        auto result = co_await execute_redis(sql);
        QueryResult qr;
        qr.success = result.success;
        qr.error = result.error;
        if (result.success) {
            // RedisQueryResult
            std::unordered_map<std::string, std::string> row;
            row["result"] = result.value.to_string();
            qr.rows.push_back(std::move(row));
        }
        co_return qr;
    }

    Task<QueryResult> execute(const std::string& sql, const std::vector<std::string>& params) override {
        // Redis
        co_return co_await execute(sql);
    }

    Task<void> begin_transaction() override {
        auto result = co_await execute_redis("MULTI");
        if (!result.success) {
            throw std::runtime_error("Failed to begin Redis transaction: " + result.error);
        }
    }

    Task<void> commit() override {
        auto result = co_await execute_redis("EXEC");
        if (!result.success) {
            throw std::runtime_error("Failed to commit Redis transaction: " + result.error);
        }
    }

    Task<void> rollback() override {
        auto result = co_await execute_redis("DISCARD");
        if (!result.success) {
            throw std::runtime_error("Failed to rollback Redis transaction: " + result.error);
        }
    }

    bool is_valid() const override {
        return context_ && context_->err == 0;
    }

    Task<bool> ping() override {
        auto result = co_await execute_redis("PING");
        co_return result.success && result.value.to_string() == "PONG";
    }

    void close() override {
        if (context_) {
            redisFree(context_);
            context_ = nullptr;
        }
    }

    std::string get_error() const override {
        return context_ ? context_->errstr : "Connection not initialized";
    }

    uint64_t get_last_insert_id() const override {
        return 0; // RedisID
    }

    uint64_t get_affected_rows() const override {
        return 0; // Redis
    }

private:
    redisContext* context_;

    Task<RedisResult> execute_redis_internal(const std::string& command) {
        if (!context_) {
            co_return RedisResult("Connection not initialized");
        }

        // Redis
        co_return co_await GlobalThreadPool::get().enqueue([this, command]() -> RedisResult {
            redisReply* reply = (redisReply*)redisCommand(context_, command.c_str());
            if (!reply) {
                return RedisResult(context_->errstr);
            }

            auto result = parse_redis_reply(reply);
            freeReplyObject(reply);
            return result;
        });
    }

    Task<RedisResult> execute_redis_args(const std::vector<std::string>& args) {
        if (!context_) {
            co_return RedisResult("Connection not initialized");
        }

        if (args.empty()) {
            co_return RedisResult("Empty command");
        }

        // Redis
        co_return co_await GlobalThreadPool::get().enqueue([this, args]() -> RedisResult {
            std::vector<const char*> argv;
            std::vector<size_t> argvlen;

            for (const auto& arg : args) {
                argv.push_back(arg.c_str());
                argvlen.push_back(arg.length());
            }

            redisReply* reply = (redisReply*)redisCommandArgv(context_,
                                                            argv.size(),
                                                            argv.data(),
                                                            argvlen.data());
            if (!reply) {
                return RedisResult(context_->errstr);
            }

            auto result = parse_redis_reply(reply);
            freeReplyObject(reply);
            return result;
        });
    }

    RedisResult parse_redis_reply(redisReply* reply) {
        if (!reply) {
            return RedisResult("Null reply");
        }

        switch (reply->type) {
            case REDIS_REPLY_STRING:
                return RedisResult(RedisValue(std::string(reply->str, reply->len)));

            case REDIS_REPLY_INTEGER:
                return RedisResult(RedisValue(reply->integer));

            case REDIS_REPLY_NIL:
                return RedisResult(RedisValue());

            case REDIS_REPLY_STATUS:
                return RedisResult(RedisValue(std::string(reply->str, reply->len)));

            case REDIS_REPLY_ERROR:
                return RedisResult(std::string(reply->str, reply->len));

            case REDIS_REPLY_ARRAY: {
                std::vector<RedisValue> array;
                for (size_t i = 0; i < reply->elements; ++i) {
                    auto element_result = parse_redis_reply(reply->element[i]);
                    if (!element_result.success) {
                        return element_result;
                    }
                    array.push_back(element_result.value);
                }
                return RedisResult(RedisValue(std::move(array)));
            }

            default:
                return RedisResult("Unknown reply type");
        }
    }
};

// Redis
class RedisDriver : public IDriver<RedisConnection> {
public:
    Task<std::unique_ptr<RedisConnection>> create_connection(
        const std::string& connection_string) override {

        auto config = parse_connection_string(connection_string);

        // 
        auto context = co_await GlobalThreadPool::get().enqueue([config]() -> redisContext* {
            struct timeval timeout = {2, 0}; // 2
            redisContext* context;

            if (config.host == "unix") {
                // Unix socket
                context = redisConnectUnixWithTimeout(config.path.c_str(), timeout);
            } else {
                // TCP
                context = redisConnectWithTimeout(config.host.c_str(), config.port, timeout);
            }

            if (!context || context->err) {
                if (context) {
                    redisFree(context);
                }
                return nullptr;
            }

            // 
            if (!config.password.empty()) {
                redisReply* reply = (redisReply*)redisCommand(context, "AUTH %s", config.password.c_str());
                if (!reply || reply->type == REDIS_REPLY_ERROR) {
                    if (reply) freeReplyObject(reply);
                    redisFree(context);
                    return nullptr;
                }
                freeReplyObject(reply);
            }

            // 
            if (config.database > 0) {
                redisReply* reply = (redisReply*)redisCommand(context, "SELECT %d", config.database);
                if (!reply || reply->type == REDIS_REPLY_ERROR) {
                    if (reply) freeReplyObject(reply);
                    redisFree(context);
                    return nullptr;
                }
                freeReplyObject(reply);
            }

            return context;
        });

        if (!context) {
            throw std::runtime_error("Failed to create Redis connection");
        }

        co_return std::make_unique<RedisConnection>(context);
    }

    bool validate_connection_string(const std::string& connection_string) const override {
        try {
            parse_connection_string(connection_string);
            return true;
        } catch (...) {
            return false;
        }
    }

    std::string get_driver_name() const override {
        return "Redis";
    }

    std::string get_version() const override {
        return "1.0.0";
    }

private:
    struct RedisConfig {
        std::string host = "localhost";
        int port = 6379;
        std::string password;
        int database = 0;
        std::string path; // for unix socket
    };

    RedisConfig parse_connection_string(const std::string& connection_string) const {
        RedisConfig config;

        // :
        // redis://[password@]host[:port][/database]
        // redis://[password@]unix:/path/to/socket[?db=database]
        // host:port
        // /path/to/socket

        std::regex redis_url_regex(R"(redis://(?:([^@]+)@)?(unix:([^?]+)|\[?([^\]/:]+)\]?(?::(\d+))?)?(?:/(\d+))?(?:\?db=(\d+))?)");
        std::regex simple_host_port_regex(R"(([^:]+):(\d+))");
        std::regex unix_socket_regex(R"(/[^?]+)");

        std::smatch match;

        if (std::regex_match(connection_string, match, redis_url_regex)) {
            // Redis URL
            if (match[1].matched) {
                config.password = match[1].str();
            }

            if (match[3].matched) {
                // Unix socket
                config.host = "unix";
                config.path = match[3].str();
            } else if (match[4].matched) {
                // TCP
                config.host = match[4].str();
                if (match[5].matched) {
                    config.port = std::stoi(match[5].str());
                }
            }

            if (match[6].matched) {
                config.database = std::stoi(match[6].str());
            } else if (match[7].matched) {
                config.database = std::stoi(match[7].str());
            }
        } else if (std::regex_match(connection_string, match, simple_host_port_regex)) {
            // host:port
            config.host = match[1].str();
            config.port = std::stoi(match[2].str());
        } else if (std::regex_match(connection_string, unix_socket_regex)) {
            // Unix socket
            config.host = "unix";
            config.path = connection_string;
        } else if (connection_string.find(':') == std::string::npos) {
            // 
            config.host = connection_string;
        } else {
            throw std::invalid_argument("Invalid Redis connection string format");
        }

        return config;
    }
};

#else // !FLOWCORO_HAS_REDIS

// Redis
class RedisConnection : public IConnection {
public:
    Task<QueryResult> execute(const std::string&) override {
        throw std::runtime_error("Redis support not compiled");
    }

    Task<QueryResult> execute(const std::string&, const std::vector<std::string>&) override {
        throw std::runtime_error("Redis support not compiled");
    }

    Task<QueryResult> begin_transaction() override {
        throw std::runtime_error("Redis support not compiled");
    }
    Task<QueryResult> commit() override {
        throw std::runtime_error("Redis support not compiled");
    }
    Task<QueryResult> rollback() override {
        throw std::runtime_error("Redis support not compiled");
    }
    bool is_valid() const override { return false; }
    Task<bool> ping() override { co_return false; }
    void close() override {}
    std::string get_error() const override { return "Redis support not compiled"; }
    uint64_t get_last_insert_id() const override { return 0; }
    uint64_t get_affected_rows() const override { return 0; }

    // Redis
    Task<RedisResult> execute_redis(const std::string&) {
        throw std::runtime_error("Redis support not compiled");
    }
    Task<RedisResult> execute_redis(const std::vector<std::string>&) {
        throw std::runtime_error("Redis support not compiled");
    }

    // 
    Task<RedisResult> set(const std::string&, const std::string&) {
        throw std::runtime_error("Redis support not compiled");
    }
    Task<RedisResult> set(const std::string&, const std::string&, std::chrono::seconds) {
        throw std::runtime_error("Redis support not compiled");
    }
    Task<RedisResult> get(const std::string&) {
        throw std::runtime_error("Redis support not compiled");
    }
    Task<RedisResult> del(const std::string&) {
        throw std::runtime_error("Redis support not compiled");
    }
    Task<RedisResult> exists(const std::string&) {
        throw std::runtime_error("Redis support not compiled");
    }

    // 
    Task<RedisResult> hset(const std::string&, const std::string&, const std::string&) {
        throw std::runtime_error("Redis support not compiled");
    }
    Task<RedisResult> hget(const std::string&, const std::string&) {
        throw std::runtime_error("Redis support not compiled");
    }
    Task<RedisResult> hgetall(const std::string&) {
        throw std::runtime_error("Redis support not compiled");
    }
    Task<RedisResult> hdel(const std::string&, const std::string&) {
        throw std::runtime_error("Redis support not compiled");
    }

    // 
    Task<RedisResult> lpush(const std::string&, const std::string&) {
        throw std::runtime_error("Redis support not compiled");
    }
    Task<RedisResult> rpush(const std::string&, const std::string&) {
        throw std::runtime_error("Redis support not compiled");
    }
    Task<RedisResult> lpop(const std::string&) {
        throw std::runtime_error("Redis support not compiled");
    }
    Task<RedisResult> rpop(const std::string&) {
        throw std::runtime_error("Redis support not compiled");
    }
    Task<RedisResult> lrange(const std::string&, int64_t, int64_t) {
        throw std::runtime_error("Redis support not compiled");
    }

    // 
    Task<RedisResult> sadd(const std::string&, const std::string&) {
        throw std::runtime_error("Redis support not compiled");
    }
    Task<RedisResult> srem(const std::string&, const std::string&) {
        throw std::runtime_error("Redis support not compiled");
    }
    Task<RedisResult> smembers(const std::string&) {
        throw std::runtime_error("Redis support not compiled");
    }
    Task<RedisResult> sismember(const std::string&, const std::string&) {
        throw std::runtime_error("Redis support not compiled");
    }

    // 
    Task<RedisResult> zadd(const std::string&, double, const std::string&) {
        throw std::runtime_error("Redis support not compiled");
    }
    Task<RedisResult> zrange(const std::string&, int64_t, int64_t) {
        throw std::runtime_error("Redis support not compiled");
    }
    Task<RedisResult> zrem(const std::string&, const std::string&) {
        throw std::runtime_error("Redis support not compiled");
    }

    // 
    Task<RedisResult> expire(const std::string&, std::chrono::seconds) {
        throw std::runtime_error("Redis support not compiled");
    }
    Task<RedisResult> ttl(const std::string&) {
        throw std::runtime_error("Redis support not compiled");
    }
};

class RedisDriver : public IDriver<RedisConnection> {
public:
    Task<std::unique_ptr<RedisConnection>> create_connection(const std::string&) override {
        throw std::runtime_error("Redis support not compiled");
    }

    bool validate_connection_string(const std::string&) const override { return false; }
    std::string get_driver_name() const override { return "Redis (Disabled)"; }
    std::string get_version() const override { return "0.0.0"; }
};

#endif // FLOWCORO_HAS_REDIS

} // namespace flowcoro::db
