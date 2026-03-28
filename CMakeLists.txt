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

# Create package folder with library and headers
set(PACKAGE_DIR ${CMAKE_BINARY_DIR}/package)

# Create package directory structure
file(MAKE_DIRECTORY ${PACKAGE_DIR})
file(MAKE_DIRECTORY ${PACKAGE_DIR}/include/libas3/ast)
file(MAKE_DIRECTORY ${PACKAGE_DIR}/include/libas3/lexer)
file(MAKE_DIRECTORY ${PACKAGE_DIR}/include/libas3/parser)
file(MAKE_DIRECTORY ${PACKAGE_DIR}/include/libas3/runtime)
file(MAKE_DIRECTORY ${PACKAGE_DIR}/include/libas3/bytecode)
file(MAKE_DIRECTORY ${PACKAGE_DIR}/lib)

# Copy header files
file(GLOB_RECURSE HEADERS "${CMAKE_SOURCE_DIR}/src/*.h")
foreach(HEADER ${HEADERS})
    file(RELATIVE_PATH REL_HEADER ${CMAKE_SOURCE_DIR}/src ${HEADER})
    get_filename_component(DEST_DIR ${PACKAGE_DIR}/include/libas3/${REL_HEADER} DIRECTORY)
    file(MAKE_DIRECTORY ${DEST_DIR})
    file(COPY ${HEADER} DESTINATION ${DEST_DIR})
endforeach()

# Copy library files
add_custom_command(TARGET as3_parser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:as3_parser>
        ${PACKAGE_DIR}/lib/
)

# Copy PDB for debugging (only in Debug builds)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_custom_command(TARGET as3_parser POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE_DIR:as3_parser>/as3_parser.pdb
            ${PACKAGE_DIR}/lib/
    )
endif()

# Create a simple install script
file(WRITE ${PACKAGE_DIR}/README.md "# as3_parser\n\nActionScript 3 Parser and Bytecode VM Library\n\n## Usage\n\nInclude the headers and link against the library:\n\n```cpp\n#include \"parser/parser.h\"\n#include \"bytecode/compiler.h\"\n#include \"bytecode/vm.h\"\n```\n")

# Print package location
message(STATUS "Package created at: ${PACKAGE_DIR}")
