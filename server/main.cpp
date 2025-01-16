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

// Глобальные переменные для работы с базой данных
sqlite3* db;
std::mutex db_mutex;

// Глобальные переменные для хранения логов в памяти
std::mutex log_mutex;
std::deque<std::pair<std::string, double>> log_temp_memory;          // Основной лог температур
std::deque<std::pair<std::string, double>> log_avg_temp_hour_memory; // Лог средних значений за час
std::deque<std::pair<std::string, double>> log_avg_temp_day_memory;  // Лог средних значений за день

// Константы
//const int MAX_TIME_DEFAULT = 24 * 60 * 60; // Максимальное время хранения записей в основном логе (24 часа)
const int MAX_TIME_DEFAULT = 60;
const int MAX_TIME_HOUR = 30 * 24 * 60 * 60; // Максимальное время хранения записей в логе за час (30 дней)
const int MAX_TIME_DAY = 365 * 24 * 60 * 60; // Максимальное время хранения записей в логе за день (1 год)
const int HOUR = 60 * 60;                   // Количество секунд в часе
const int DAY = 24 * 60 * 60;               // Количество секунд в дне
const double TIME_DELAY = 1.0;             // Таймаут для чтения данных
const int SYNC_INTERVAL = 6;               // Интервал синхронизации с диском

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

    // Генерация временной метки для удаления
    time_t now = time(nullptr);
    struct tm tm;
    localtime_s(&tm, &now);
    char buffer[80];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
    std::string current_time(buffer);

    // Формирование SQL-запроса
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

    // Синхронизация основного лога
    for (const auto& entry : log_temp_memory) {
        insertIntoTable("temperatures", entry.first, entry.second);
    }
    deleteOldEntries("temperatures", MAX_TIME_DEFAULT); // Удаление старых записей из temperatures
    log_temp_memory.clear();

    // Синхронизация лога средних значений за час
    for (const auto& entry : log_avg_temp_hour_memory) {
        insertIntoTable("avg_temp_hour", entry.first, entry.second);
    }
    deleteOldEntries("avg_temp_hour", MAX_TIME_HOUR); // Удаление старых записей из avg_temp_hour
    log_avg_temp_hour_memory.clear();

    // Синхронизация лога средних значений за день
    for (const auto& entry : log_avg_temp_day_memory) {
        insertIntoTable("avg_temp_day", entry.first, entry.second);
    }
    deleteOldEntries("avg_temp_day", MAX_TIME_DAY); // Удаление старых записей из avg_temp_day
    log_avg_temp_day_memory.clear();
}

// Удаление старых записей из памяти

// Вычисление среднего значения температуры за последние max_age_seconds
double calculateAverageTemperature(const std::deque<std::pair<std::string, double>>& log_memory, int max_age_seconds) {
    std::lock_guard<std::mutex> lock(log_mutex);
    std::time_t now = std::time(nullptr);
    double sum = 0.0;
    int count = 0;

    for (const auto& entry : log_memory) {
        std::tm tm = {};
        std::istringstream ss(entry.first);
        ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
        if (ss.fail()) continue;

        std::time_t entry_time = std::mktime(&tm);
        if (now - entry_time < max_age_seconds) {
            sum += entry.second;
            count++;
        }
    }

    return (count > 0) ? (sum / count) : 0.0;
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
            double avg_hour = calculateAverageTemperature(log_temp_memory, HOUR);
            std::string timestamp = getCurrentTime();
            {
                std::lock_guard<std::mutex> lock(log_mutex);
                log_avg_temp_hour_memory.emplace_back(timestamp, avg_hour);
            }
            counter_avg_hour = 0;
        }

        // Каждые 24 часа вычисляем среднее значение температуры за последний день
        if (counter_avg_day >= DAY) {
            double avg_day = calculateAverageTemperature(log_temp_memory, DAY);
            std::string timestamp = getCurrentTime();
            {
                std::lock_guard<std::mutex> lock(log_mutex);
                log_avg_temp_day_memory.emplace_back(timestamp, avg_day);
            }
            counter_avg_day = 0;
        }

        // Синхронизация логов с базой данных каждую минуту
        if (sync_counter >= SYNC_INTERVAL) {
            syncLogsToDatabase();
            cout << "data updated" << endl;
            sync_counter = 0;
        }
    }

    sqlite3_close(db);
    return 0;
}