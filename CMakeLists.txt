cmake_minimum_required(VERSION 3.12)

project(lab4)

add_library(default INTERFACE)

if(MSVC)
  target_compile_options(default INTERFACE /W4 /WX)
else()
  target_compile_options(default INTERFACE -Wall -Wextra -Wpedantic -Werror)
endif()
target_compile_features(default INTERFACE cxx_std_20)

add_executable(lab lab5.cpp errors.cpp threads.hpp)
target_link_libraries(lab PUBLIC default)

add_executable(test test.cpp errors.cpp threads.hpp)
target_link_libraries(test PUBLIC default)