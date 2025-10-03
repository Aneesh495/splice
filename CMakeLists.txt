cmake_minimum_required(VERSION 3.24)
project(splice VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SPLICE_ENABLE_ASAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_library(splice_options INTERFACE)
target_compile_features(splice_options INTERFACE cxx_std_20)
target_compile_options(splice_options INTERFACE
  $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wsign-conversion;-Wshadow;-Wformat=2;-Werror=return-type>
  $<$<CXX_COMPILER_ID:MSVC>:/W4;/permissive->
)
if(SPLICE_ENABLE_ASAN AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(splice_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(splice_options INTERFACE -fsanitize=address,undefined)
endif()

add_executable(splice src/app/main.cpp)
target_link_libraries(splice PRIVATE splice_options)
target_include_directories(splice PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

include(CTest)
if(BUILD_TESTING)
  add_test(NAME splice_version COMMAND splice --version)
  add_test(NAME splice_help COMMAND splice --help)
endif()

install(TARGETS splice RUNTIME DESTINATION bin)
