cmake_minimum_required(VERSION 3.12)

project(lab4)

add_executable(lab lab5.cpp errors.cpp threads.hpp)
target_compile_features(lab PUBLIC cxx_std_20)

add_executable(test test.cpp errors.cpp threads.hpp)
target_compile_features(test PUBLIC cxx_std_20)