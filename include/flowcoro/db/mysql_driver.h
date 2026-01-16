#pragma once

#include "connection_pool.h"

// MySQL
#ifdef FLOWCORO_HAS_MYSQL
#include <mysql/mysql.h>
#endif

#include <regex>

namespace flowcoro::db {

#ifdef FLOWCORO_HAS_MYSQL

// MySQL
class MySQLConnection : public IConnection {
public:
    MySQLConnection(MYSQL* mysql) : mysql_(mysql) {}

    ~MySQLConnection() override {
        close();
    }

    Task<QueryResult> execute(const std::string& sql) override {
        co_return co_await execute_internal(sql, {});
    }

    Task<QueryResult> execute(const std::string& sql,
                             const std::vector<std::string>& params) override {
        co_return co_await execute_internal(sql, params);
    }

    Task<void> begin_transaction() override {
        auto result = co_await execute("BEGIN");
        if (!result.success) {
            throw std::runtime_error("Failed to begin transaction: " + result.error);
        }
    }

    Task<void> commit() override {
        auto result = co_await execute("COMMIT");
        if (!result.success) {
            throw std::runtime_error("Failed to commit transaction: " + result.error);
        }
    }

    Task<void> rollback() override {
        auto result = co_await execute("ROLLBACK");
        if (!result.success) {
            throw std::runtime_error("Failed to rollback transaction: " + result.error);
        }
    }

    bool is_valid() const override {
        return mysql_ && mysql_ping(mysql_) == 0;
    }

    Task<bool> ping() override {
        if (!mysql_) co_return false;

        // pingmysql_ping
        co_return co_await GlobalThreadPool::get().enqueue([this]() -> bool {
            return mysql_ping(mysql_) == 0;
        });
    }

    void close() override {
        if (mysql_) {
            mysql_close(mysql_);
            mysql_ = nullptr;
        }
    }

    std::string get_error() const override {
        return mysql_ ? mysql_error(mysql_) : "Connection not initialized";
    }

    uint64_t get_last_insert_id() const override {
        return mysql_ ? mysql_insert_id(mysql_) : 0;
    }

    uint64_t get_affected_rows() const override {
        return mysql_ ? mysql_affected_rows(mysql_) : 0;
    }

private:
    MYSQL* mysql_;

    Task<QueryResult> execute_internal(const std::string& sql,
                                      const std::vector<std::string>& params) {
        QueryResult result;

        if (!mysql_) {
            result.error = "Connection not initialized";
            co_return result;
        }

        // 
        co_return co_await GlobalThreadPool::get().enqueue([this, sql, params]() -> QueryResult {
            QueryResult result;

            try {
                // SQL
                std::string prepared_sql = prepare_sql(sql, params);

                // 
                if (mysql_real_query(mysql_, prepared_sql.c_str(), prepared_sql.length()) != 0) {
                    result.error = mysql_error(mysql_);
                    return result;
                }

                // 
                MYSQL_RES* mysql_result = mysql_store_result(mysql_);
                if (mysql_result) {
                    // SELECT
                    result = process_result_set(mysql_result);
                    mysql_free_result(mysql_result);
                } else {
                    // INSERT, UPDATE, DELETE
                    if (mysql_errno(mysql_) == 0) {
                        result.success = true;
                        result.affected_rows = mysql_affected_rows(mysql_);
                        result.insert_id = mysql_insert_id(mysql_);
                    } else {
                        result.error = mysql_error(mysql_);
                    }
                }

            } catch (const std::exception& e) {
                result.error = e.what();
            }

            return result;
        });
    }

    std::string prepare_sql(const std::string& sql, const std::vector<std::string>& params) {
        if (params.empty()) return sql;

        std::string result = sql;
        size_t param_index = 0;
        size_t pos = 0;

        while ((pos = result.find('?', pos)) != std::string::npos && param_index < params.size()) {
            // 
            std::string escaped_param = escape_string(params[param_index]);
            result.replace(pos, 1, "'" + escaped_param + "'");
            pos += escaped_param.length() + 2; // +2 for quotes
            param_index++;
        }

        return result;
    }

    std::string escape_string(const std::string& input) {
        if (!mysql_) return input;

        std::vector<char> escaped(input.length() * 2 + 1);
        mysql_real_escape_string(mysql_, escaped.data(), input.c_str(), input.length());
        return std::string(escaped.data());
    }

    QueryResult process_result_set(MYSQL_RES* mysql_result) {
        QueryResult result;
        result.success = true;

        MYSQL_FIELD* fields = mysql_fetch_fields(mysql_result);
        unsigned int num_fields = mysql_num_fields(mysql_result);

        MYSQL_ROW row;
        while ((row = mysql_fetch_row(mysql_result))) {
            std::unordered_map<std::string, std::string> row_map;
            unsigned long* lengths = mysql_fetch_lengths(mysql_result);

            for (unsigned int i = 0; i < num_fields; i++) {
                std::string field_name = fields[i].name;
                std::string field_value = row[i] ? std::string(row[i], lengths[i]) : "";
                row_map[field_name] = field_value;
            }

            result.rows.push_back(std::move(row_map));
        }

        return result;
    }
};

// MySQL
class MySQLDriver : public IDriver<MySQLConnection> {
public:
    MySQLDriver() {
        // MySQL
        mysql_library_init(0, nullptr, nullptr);
    }

    ~MySQLDriver() {
        mysql_library_end();
    }

    Task<std::unique_ptr<MySQLConnection>> create_connection(
        const std::string& connection_string) override {

        // 
        co_return co_await GlobalThreadPool::get().enqueue([this, connection_string]()
            -> std::unique_ptr<MySQLConnection> {

            try {
                auto params = parse_connection_string(connection_string);

                MYSQL* mysql = mysql_init(nullptr);
                if (!mysql) {
                    throw std::runtime_error("Failed to initialize MySQL connection");
                }

                // 
                set_connection_options(mysql, params);

                // 
                if (!mysql_real_connect(mysql,
                                       params.host.c_str(),
                                       params.user.c_str(),
                                       params.password.c_str(),
                                       params.database.c_str(),
                                       params.port,
                                       nullptr, 0)) {
                    std::string error = mysql_error(mysql);
                    mysql_close(mysql);
                    throw std::runtime_error("Failed to connect to MySQL: " + error);
                }

                return std::make_unique<MySQLConnection>(mysql);

            } catch (const std::exception& e) {
                LOG_ERROR("Failed to create MySQL connection: %s", e.what());
                throw;
            }
        });
    }

    bool validate_connection_string(const std::string& connection_string) const override {
        try {
            auto params = parse_connection_string(connection_string);
            return !params.host.empty() && !params.user.empty();
        } catch (...) {
            return false;
        }
    }

    std::string get_driver_name() const override {
        return "MySQL";
    }

    std::string get_version() const override {
        return mysql_get_client_info();
    }

private:
    struct ConnectionParams {
        std::string host = "localhost";
        int port = 3306;
        std::string user;
        std::string password;
        std::string database;
        std::string charset = "utf8mb4";
        int connect_timeout = 10;
        int read_timeout = 30;
        int write_timeout = 30;
        bool auto_reconnect = true;
    };

    ConnectionParams parse_connection_string(const std::string& conn_str) const {
        ConnectionParams params;

        // : mysql://user:password@host:port/database?param=value
        std::regex url_regex(R"(mysql://([^:]+)(?::([^@]*))?@([^:/]+)(?::(\d+))?/([^?]+)(?:\?(.+))?)");
        std::smatch matches;

        if (std::regex_match(conn_str, matches, url_regex)) {
            params.user = matches[1].str();
            if (matches[2].matched) params.password = matches[2].str();
            params.host = matches[3].str();
            if (matches[4].matched) params.port = std::stoi(matches[4].str());
            params.database = matches[5].str();

            if (matches[6].matched) {
                parse_query_params(matches[6].str(), params);
            }
        } else {
            throw std::invalid_argument("Invalid MySQL connection string format");
        }

        return params;
    }

    void parse_query_params(const std::string& query_string, ConnectionParams& params) const {
        std::regex param_regex(R"(([^&=]+)=([^&]*))");
        std::sregex_iterator iter(query_string.begin(), query_string.end(), param_regex);
        std::sregex_iterator end;

        for (; iter != end; ++iter) {
            std::string key = (*iter)[1].str();
            std::string value = (*iter)[2].str();

            if (key == "charset") params.charset = value;
            else if (key == "connect_timeout") params.connect_timeout = std::stoi(value);
            else if (key == "read_timeout") params.read_timeout = std::stoi(value);
            else if (key == "write_timeout") params.write_timeout = std::stoi(value);
            else if (key == "auto_reconnect") params.auto_reconnect = (value == "true" || value == "1");
        }
    }

    void set_connection_options(MYSQL* mysql, const ConnectionParams& params) const {
        // 
        mysql_options(mysql, MYSQL_SET_CHARSET_NAME, params.charset.c_str());

        // 
        mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &params.connect_timeout);
        mysql_options(mysql, MYSQL_OPT_READ_TIMEOUT, &params.read_timeout);
        mysql_options(mysql, MYSQL_OPT_WRITE_TIMEOUT, &params.write_timeout);

        // 
        mysql_options(mysql, MYSQL_OPT_RECONNECT, &params.auto_reconnect);

        // 
        unsigned long client_flags = CLIENT_MULTI_STATEMENTS;
        mysql_options(mysql, MYSQL_INIT_COMMAND, "SET SESSION sql_mode='STRICT_TRANS_TABLES'");
    }
};

// 
inline std::unique_ptr<ConnectionPool<MySQLConnection>> create_mysql_pool(
    const std::string& connection_string,
    const PoolConfig& config = {}) {

    auto driver = std::make_unique<MySQLDriver>();
    return std::make_unique<ConnectionPool<MySQLConnection>>(
        std::move(driver), connection_string, config);
}

#else // !FLOWCORO_HAS_MYSQL

// MySQL
class MySQLConnection : public IConnection {
public:
    Task<QueryResult> execute(const std::string&) override {
        throw std::runtime_error("MySQL support not compiled");
    }

    Task<QueryResult> execute(const std::string&, const std::vector<std::string>&) override {
        throw std::runtime_error("MySQL support not compiled");
    }

    Task<QueryResult> begin_transaction() override {
        throw std::runtime_error("MySQL support not compiled");
    }
    Task<QueryResult> commit() override {
        throw std::runtime_error("MySQL support not compiled");
    }
    Task<QueryResult> rollback() override {
        throw std::runtime_error("MySQL support not compiled");
    }
    bool is_valid() const override { return false; }
    Task<bool> ping() override { co_return false; }
    void close() override {}
    std::string get_error() const override { return "MySQL support not compiled"; }
    uint64_t get_last_insert_id() const override { return 0; }
    uint64_t get_affected_rows() const override { return 0; }
};

class MySQLDriver : public IDriver<MySQLConnection> {
public:
    Task<std::unique_ptr<MySQLConnection>> create_connection(const std::string&) override {
        throw std::runtime_error("MySQL support not compiled");
    }

    bool validate_connection_string(const std::string&) const override { return false; }
    std::string get_driver_name() const override { return "MySQL (Disabled)"; }
    std::string get_version() const override { return "0.0.0"; }
};

inline std::unique_ptr<ConnectionPool<MySQLConnection>> create_mysql_pool(
    const std::string&, const PoolConfig& = {}) {
    throw std::runtime_error("MySQL support not compiled");
}

#endif // FLOWCORO_HAS_MYSQL

} // namespace flowcoro::db
