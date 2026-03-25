cmake_minimum_required(VERSION 3.15)
project(as3_parser VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(${CMAKE_SOURCE_DIR})

add_library(as3_parser
    src/ast/ast_nodes.cpp
    src/lexer/lexer.cpp
    src/parser/parser.cpp
    src/runtime/value.cpp
    src/bytecode/opcode.cpp
    src/bytecode/chunk.cpp
    src/bytecode/compiler.cpp
    src/bytecode/vm.cpp
)

target_include_directories(as3_parser PUBLIC
    ${CMAKE_SOURCE_DIR}/src/ast
    ${CMAKE_SOURCE_DIR}/src/lexer
    ${CMAKE_SOURCE_DIR}/src/parser
    ${CMAKE_SOURCE_DIR}/src/runtime
    ${CMAKE_SOURCE_DIR}/src/bytecode
)

# Unified test executable
add_executable(as3_tests
    test/main.cpp
    test/parser_tests.cpp
    test/bytecode_tests.cpp
    test/vm_tests.cpp
    test/benchmark_tests.cpp
)

target_include_directories(as3_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/test
)

target_link_libraries(as3_tests as3_parser)

enable_testing()
add_test(NAME AllTests COMMAND as3_tests)
