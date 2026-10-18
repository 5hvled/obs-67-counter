cmake_minimum_required(VERSION 3.28)
project(obs-67-counter VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(libobs REQUIRED)

add_library(obs-67-counter MODULE
  src/plugin-main.cpp
  src/sixty-seven-filter.cpp
  src/sixty-seven-filter.hpp
  src/hand-detector.cpp
  src/hand-detector.hpp
  src/counter-state.cpp
  src/counter-state.hpp
)

target_link_libraries(obs-67-counter PRIVATE OBS::libobs)

if(MSVC)
  target_compile_options(obs-67-counter PRIVATE /W4 /permissive-)
endif()

set_target_properties(obs-67-counter PROPERTIES PREFIX "")
