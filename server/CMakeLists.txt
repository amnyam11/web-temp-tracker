cmake_minimum_required(VERSION 3.12)
project(TemperatureMonitor)

# Установите политику CMP0167 для подавления предупреждения
# cmake_policy(SET CMP0167 NEW)



# Поиск библиотеки SQLite3
find_package(SQLite3 REQUIRED)

# Найдите Boost с необходимыми компонентами
find_package(Boost 1.83.0 REQUIRED COMPONENTS system filesystem date_time)

# Проверьте, что Boost найден
if (Boost_FOUND)
    message(STATUS "Boost found: ${Boost_VERSION}")
    message(STATUS "Boost include dirs: ${Boost_INCLUDE_DIRS}")
    message(STATUS "Boost library dirs: ${Boost_LIBRARY_DIRS}")
    include_directories(${Boost_INCLUDE_DIRS})
    link_directories(${Boost_LIBRARY_DIRS})
else()
    message(FATAL_ERROR "Boost not found!")
endif()

if(SQLite3_FOUND)
	message(STATUS "SQLite3 found: ${SQLite3_VERSION}")
    message(STATUS "SQLite3 include dirs: ${SQLite3_INCLUDE_DIRS}")
    message(STATUS "SQLite3 library dirs: ${SQLite3_LIBRARY_DIRS}")
    include_directories(${Boost_INCLUDE_DIRS})
    link_directories(${Boost_LIBRARY_DIRS})
else()
    message(FATAL_ERROR "Boost not found!")
endif()

# Добавьте исполняемый файл для сервера
add_executable(server server.cpp)
target_link_libraries(server 
    Boost::system 
    Boost::filesystem 
    Boost::date_time 
    SQLite::SQLite3 
)

# Добавьте исполняемый файл для main.cpp
add_executable(temperature_monitor main.cpp)
target_link_libraries(temperature_monitor 
    Boost::system 
    Boost::filesystem  
    Boost::date_time 
    SQLite::SQLite3 
)

# Добавьте исполняемый файл для simulator.cpp
add_executable(simulator simulator.cpp)
target_link_libraries(simulator)
