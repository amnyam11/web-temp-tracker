#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <iostream>
#include <sqlite3.h>
#include <mutex>
#include <string>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace pt = boost::property_tree;

using tcp = asio::ip::tcp;

// Глобальные переменные для работы с базой данных
sqlite3* db;
std::mutex db_mutex;

// Инициализация базы данных
void initializeDatabase() {
    int rc = sqlite3_open("temperature.db", &db);
    if (rc) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        exit(1);
    }
}

// Выполнение SQL-запроса и возврат результата в формате JSON
std::string executeQuery(const std::string& query) {
    std::lock_guard<std::mutex> lock(db_mutex);
    pt::ptree root;
    pt::ptree data;

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        return "[]";
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        pt::ptree row;
        row.put("timestamp", reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        row.put("value", sqlite3_column_double(stmt, 1));
        
        data.push_back(std::make_pair("", row));
    }

    root.add_child("data", data);
    sqlite3_finalize(stmt);

    std::ostringstream oss;
    pt::write_json(oss, root);
    return oss.str();
}

// Обработчик HTTP-запросов
void handle_request(http::request<http::string_body>& req, http::response<http::string_body>& res) {
    res.version(req.version());
    res.keep_alive(false);

    if (req.method() == http::verb::get) {
        if (req.target() == "/temperatures") {
            // Получение данных из таблицы temperatures
            std::string json_data = executeQuery("SELECT timestamp, value FROM temperatures;");
            res.result(http::status::ok);
            res.set(http::field::content_type, "application/json");
            res.body() = json_data;
        } else if (req.target() == "/avg_temp_hour") {
            // Получение данных из таблицы avg_temp_hour
            std::string json_data = executeQuery("SELECT timestamp, value FROM avg_temp_hour;");
            res.result(http::status::ok);
            res.set(http::field::content_type, "application/json");
            res.body() = json_data;
        } else if (req.target() == "/avg_temp_day") {
            // Получение данных из таблицы avg_temp_day
            std::string json_data = executeQuery("SELECT timestamp, value FROM avg_temp_day;");
            res.result(http::status::ok);
            res.set(http::field::content_type, "application/json");
            res.body() = json_data;
        } else {
            res.result(http::status::not_found);
            res.body() = "Resource not found";
        }
    } else {
        res.result(http::status::method_not_allowed);
        res.body() = "Method not allowed";
    }

    res.prepare_payload();
}

// Запуск сервера
void run_server(asio::io_context& io_context, unsigned short port) {
    tcp::acceptor acceptor(io_context, {tcp::v4(), port});
    std::cout << "Server is running on port " << port << std::endl;

    for (;;) {
        tcp::socket socket(io_context);
        acceptor.accept(socket);

        beast::flat_buffer buffer;
        http::request<http::string_body> req;
        http::read(socket, buffer, req);

        http::response<http::string_body> res;
        handle_request(req, res);

        http::write(socket, res);
    }
}

int main() {
    try {
        asio::io_context io_context;

        // Инициализация базы данных
        initializeDatabase();

        // Запуск сервера на порту 8080
        run_server(io_context, 8080);
    } catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}