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
)

target_include_directories(as3_parser PUBLIC
    ${CMAKE_SOURCE_DIR}/src/ast
    ${CMAKE_SOURCE_DIR}/src/lexer
    ${CMAKE_SOURCE_DIR}/src/parser
)

add_executable(as3_parser_test
    src/main.cpp
)

target_link_libraries(as3_parser_test as3_parser)

enable_testing()
add_test(NAME ParserTest COMMAND as3_parser_test)
