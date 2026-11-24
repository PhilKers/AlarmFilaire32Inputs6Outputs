# Compilation PC du noyau portable : tests unitaires + simulateur console.
# Le firmware STM32 est construit à part (projet STM32CubeIDE, voir docs/).
cmake_minimum_required(VERSION 3.13)
project(alarme32_core C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)

file(GLOB CORE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/core/*.c)
add_library(alarm_core STATIC ${CORE_SRC})
target_include_directories(alarm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)

add_executable(test_core tests/test_core.c)
target_link_libraries(test_core alarm_core)

add_executable(alarm_sim sim/sim_main.c)
target_link_libraries(alarm_sim alarm_core)

enable_testing()
add_test(NAME noyau COMMAND test_core)
