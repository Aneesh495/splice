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

add_executable(splice
  src/app/main.cpp
  src/syntax/ast.cpp
  src/syntax/lexer.cpp
  src/syntax/parser.cpp
  src/syntax/token.cpp
  src/expand/expander.cpp
  src/expand/state.cpp
  src/plan/plan.cpp
  src/builtins/builtins.cpp
  src/runtime/runtime.cpp
  src/history/history.cpp
  src/inspect/trace.cpp
  src/inspect/replay.cpp
  src/interactive/editor.cpp
  src/interactive/completion.cpp
  src/task/runner.cpp
)
target_link_libraries(splice PRIVATE splice_options)
target_include_directories(splice PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

include(CTest)
if(BUILD_TESTING)
  add_test(NAME splice_version COMMAND splice --version)
  add_test(NAME splice_help COMMAND splice --help)
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME splice_syntax_cases
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/syntax_cases.py $<TARGET_FILE:splice>)
    add_test(NAME splice_runtime_cases
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime_cases.py $<TARGET_FILE:splice>)
    add_test(NAME splice_runtime_fault_cases
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime_fault_cases.py $<TARGET_FILE:splice>)
    add_test(NAME splice_pty_cases
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/pty_cases.py $<TARGET_FILE:splice>)
    add_test(NAME splice_lsp_cases
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/lsp_cases.py $<TARGET_FILE:splice-lsp>)
    add_test(NAME splice_task_cases
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/task_cases.py $<TARGET_FILE:splice>)
  endif()
endif()

install(TARGETS splice RUNTIME DESTINATION bin)

add_executable(splice-lsp
  src/language_tools/lsp_main.cpp
  src/syntax/ast.cpp
  src/syntax/lexer.cpp
  src/syntax/parser.cpp
  src/syntax/token.cpp
)
target_link_libraries(splice-lsp PRIVATE splice_options)
target_include_directories(splice-lsp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
install(TARGETS splice-lsp RUNTIME DESTINATION bin)
