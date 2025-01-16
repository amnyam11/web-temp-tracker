#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <sys/time.h>
#endif

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iomanip>
#include <ctime>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <boost/asio.hpp>

#include "my_serial.hpp"

#include <sqlite3.h>

using namespace std;

sqlite3* db;
std::mutex db_mutex;

std::mutex log_mutex;
std::deque<std::pair<std::string, double>> log_temp_memory; // Основной лог температур

// Константы
const double TIME_DELAY = 10.0;             // Таймаут для чтения данных
const int MAX_TIME_DEFAULT = 24 * 60 * 60; // Максимальное время хранения записей в основном логе (24 часа)
const int MAX_TIME_HOUR = 30 * 24 * 60 * 60; // Максимальное время хранения записей в логе за час (30 дней)
const int MAX_TIME_DAY = 365 * 24 * 60 * 60; // Максимальное время хранения записей в логе за день (1 год)
const int HOUR = 60 * 60 / TIME_DELAY;                   // Интервал записи 
const int DAY = 24 * 60 * 60 / TIME_DELAY;               // Интервал записи
const int SYNC_INTERVAL = 60 / TIME_DELAY;               // Интервал синхронизации с бд 

// Функция для преобразования любого типа в строку
template<class T>
std::string to_string(const T& v) {
    std::ostringstream ss;
    ss << v;
    return ss.str();
}

// Получение текущего времени в формате "YYYY-MM-DD HH:MM:SS"
std::string getCurrentTime() {
    time_t now = time(nullptr);
    struct tm tm;
    localtime_s(&tm, &now);
    char buffer[80];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
    return string(buffer);
}

// Инициализация базы данных и создание таблиц
void initializeDatabase() {
    int rc = sqlite3_open("temperature.db", &db);
    if (rc) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        exit(1);
    }

    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS temperatures (
            timestamp TEXT PRIMARY KEY,
            value REAL
        );
        CREATE TABLE IF NOT EXISTS avg_temp_hour (
            timestamp TEXT PRIMARY KEY,
            value REAL
        );
        CREATE TABLE IF NOT EXISTS avg_temp_day (
            timestamp TEXT PRIMARY KEY,
            value REAL
        );
    )";

    char* errMsg = 0;
    rc = sqlite3_exec(db, sql, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
    }
}

void insertIntoTable(const std::string& table, const std::string& timestamp, double value) {
    std::lock_guard<std::mutex> lock(db_mutex);
    std::string sql = "INSERT INTO " + table + " (timestamp, value) VALUES ('" + timestamp + "', " + to_string(value) + ");";
    char* errMsg = 0;
    int rc = sqlite3_exec(db, sql.c_str(), 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
    }
}

void deleteOldEntries(const std::string& table, int max_age_seconds) {
    std::lock_guard<std::mutex> lock(db_mutex);

    time_t now = time(nullptr);
    struct tm tm;
    localtime_s(&tm, &now);
    char buffer[80];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
    std::string current_time(buffer);

    std::string sql = "DELETE FROM " + table + " WHERE timestamp < datetime('" + current_time + "', '-" + to_string(max_age_seconds) + " seconds');";
    char* errMsg = 0;
    int rc = sqlite3_exec(db, sql.c_str(), 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
    } else {
        std::cout << "Deleted old entries from " << table << " older than " << max_age_seconds << " seconds" << std::endl;
    }
}

// Синхронизация логов из памяти в базу данных
void syncLogsToDatabase() {
    std::lock_guard<std::mutex> lock(log_mutex);
    for (const auto& entry : log_temp_memory) {
        insertIntoTable("temperatures", entry.first, entry.second);
    }
    deleteOldEntries("temperatures", MAX_TIME_DEFAULT); 
    log_temp_memory.clear();
}


// Вычисление средней температуры
double calculateAverageTemperature(std::string type) {
    std::lock_guard<std::mutex> lock(db_mutex);
    std::string hour_command = "SELECT AVG(value) FROM temperatures WHERE timestamp >= datetime('now', '-1 hour');";
    std::string day_command = "SELECT AVG(value) FROM temperatures WHERE timestamp >= datetime('now', '-1 day');";
    std::string sql = type == "hour" ? hour_command : day_command;
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        return 0.0;
    }

    double avg = 0.0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        avg = sqlite3_column_double(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return avg;
}
// Проверка на наличие нулевых байтов в строке
bool containsNullBytes(const std::string& str) {
    return str.find('\x00') != std::string::npos;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <port>" << std::endl;
        return -1;
    }

    cplib::SerialPort smport(std::string(argv[1]), cplib::SerialPort::BAUDRATE_115200);
    if (!smport.IsOpen()) {
        std::cout << "Failed to open port '" << argv[1] << "'! Terminating..." << std::endl;
        return -2;
    }

    initializeDatabase();

    std::string mystr;
    smport.SetTimeout(TIME_DELAY);

    int counter_avg_hour = 0;
    int counter_avg_day = 0;
    int sync_counter = 0;

    for (;;) {
        smport >> mystr;
        if (!mystr.empty() && !containsNullBytes(mystr)) {
            // Валидация данных
            bool is_valid = true;
            for (char ch : mystr) {
                if (!isdigit(ch) && ch != '.' && ch != '-') {
                    is_valid = false;
                    break;
                }
            }
            if (is_valid) {
                std::cout << "Got: " << mystr << std::endl;
                double temp = stod(mystr);
                std::string timestamp = getCurrentTime();
                {
                    std::lock_guard<std::mutex> lock(log_mutex);
                    log_temp_memory.emplace_back(timestamp, temp);
                }
            }
        } else {
            std::cout << "Got nothing" << std::endl;
        }

        counter_avg_hour++;
        counter_avg_day++;
        sync_counter++;

        // Каждый час вычисляем среднее значение температуры за последний час
        if (counter_avg_hour >= HOUR) {
            double avg_hour = calculateAverageTemperature("hour");
            std::string timestamp = getCurrentTime();
            insertIntoTable("avg_temp_hour", timestamp, avg_hour); // Запись в базу данных
            deleteOldEntries("avg_temp_hour", MAX_TIME_HOUR);
            counter_avg_hour = 0;
        }

        // Каждые 24 часа вычисляем среднее значение температуры за последний день
        if (counter_avg_day >= DAY) {
            double avg_day = calculateAverageTemperature("day");
            std::string timestamp = getCurrentTime();
            insertIntoTable("avg_temp_day", timestamp, avg_day); // Запись в базу данных
            deleteOldEntries("avg_temp_day", MAX_TIME_DAY); 
            counter_avg_day = 0;
        }

        // Синхронизация логов с бд
        if (sync_counter >= SYNC_INTERVAL) {
            syncLogsToDatabase();
            cout << "data updated" << endl;
            sync_counter = 0;
        }
    }

    sqlite3_close(db);
    return 0;
}