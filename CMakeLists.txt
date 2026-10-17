cmake_minimum_required(VERSION 3.15)
project(as3_parser VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Built on its own, or inside another project (Phoenix): paths are this
# folder's, and the tests and the package are only made on its own.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(AS3_TOP_LEVEL ON)
else()
    set(AS3_TOP_LEVEL OFF)
endif()

add_library(as3_parser
    src/ast/ast_nodes.cpp
    src/lexer/lexer.cpp
    src/parser/parser.cpp
    src/runtime/value.cpp
    src/bytecode/opcode.cpp
    src/bytecode/chunk.cpp
    src/bytecode/compiler.cpp
    src/bytecode/vm.cpp
    src/bytecode/vm_runtime.cpp
    src/runtime/builtins_core.cpp
    src/runtime/builtins_array.cpp
    src/runtime/builtins_string.cpp
    src/runtime/builtins_date.cpp
    src/runtime/builtins_json.cpp
    src/runtime/builtins_utils.cpp
    src/runtime/builtins_xml.cpp
    src/abc/abc_file.cpp
    src/abc/abc_opcodes.cpp
    src/abc/abc_verifier.cpp
    src/abc/abc_builder.cpp
    src/abc/abc_player.cpp
    src/abc/abc_model.cpp
    src/abc/abc_compiler.cpp
    src/avm1/avm1_compiler.cpp
    src/avm1/avm1_disassembler.cpp
    src/avm1/avm1_interpreter.cpp
    src/abc/abc_domain.cpp
    src/js/js_writer.cpp
    src/js/js_translator.cpp
    src/js/js_runtime.cpp
    src/js/as2_translator.cpp
)

# as3.js, the runtime translated JavaScript needs, embedded for js::runtime().
set(AS3_JS_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/js/as3.js)
set(AS3_JS_EMBEDDED ${CMAKE_CURRENT_BINARY_DIR}/generated/as3_js.inc)
add_custom_command(
    OUTPUT ${AS3_JS_EMBEDDED}
    COMMAND ${CMAKE_COMMAND} -DINPUT=${AS3_JS_SOURCE} -DOUTPUT=${AS3_JS_EMBEDDED}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/src/js/embed.cmake
    DEPENDS ${AS3_JS_SOURCE} ${CMAKE_CURRENT_SOURCE_DIR}/src/js/embed.cmake
    COMMENT "Embedding as3.js")
# avm1.js, the helpers ActionScript 1 and 2 translated to JavaScript needs.
set(AVM1_JS_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/js/avm1.js)
set(AVM1_JS_EMBEDDED ${CMAKE_CURRENT_BINARY_DIR}/generated/avm1_js.inc)
add_custom_command(
    OUTPUT ${AVM1_JS_EMBEDDED}
    COMMAND ${CMAKE_COMMAND} -DINPUT=${AVM1_JS_SOURCE} -DOUTPUT=${AVM1_JS_EMBEDDED}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/src/js/embed.cmake
    DEPENDS ${AVM1_JS_SOURCE} ${CMAKE_CURRENT_SOURCE_DIR}/src/js/embed.cmake
    COMMENT "Embedding avm1.js")
target_sources(as3_parser PRIVATE ${AS3_JS_EMBEDDED} ${AVM1_JS_EMBEDDED})
target_include_directories(as3_parser PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

target_include_directories(as3_parser PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ast
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lexer
    ${CMAKE_CURRENT_SOURCE_DIR}/src/parser
    ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bytecode
    ${CMAKE_CURRENT_SOURCE_DIR}/src/abc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/js
)

if(NOT AS3_TOP_LEVEL)
    return()
endif()

# Unified test executable
add_executable(as3_tests
    test/main.cpp
    test/parser_tests.cpp
    test/bytecode_tests.cpp
    test/vm_tests.cpp
    test/as2_tests.cpp
    test/benchmark_tests.cpp
    test/language_tests.cpp
    test/bunnypaint_tests.cpp
    test/native_class_tests.cpp
    test/library_tests.cpp
    test/error_tests.cpp
    test/xml_tests.cpp
    test/abc_tests.cpp
    test/abc_compiler_tests.cpp
    test/avm1_tests.cpp
    test/avm1_run_tests.cpp
    test/abc_domain_tests.cpp
    test/js_tests.cpp
    test/js_as2_tests.cpp
)

target_include_directories(as3_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/test
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
file(MAKE_DIRECTORY ${PACKAGE_DIR}/include/libas3/abc)
file(MAKE_DIRECTORY ${PACKAGE_DIR}/include/libas3/js)
file(MAKE_DIRECTORY ${PACKAGE_DIR}/lib)

# Copy header files
file(GLOB_RECURSE HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
foreach(HEADER ${HEADERS})
    file(RELATIVE_PATH REL_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/src ${HEADER})
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

# Copy PDB for debugging (only in Debug builds). A single-configuration
# generator (Ninja) has MSVC write a static library's compile PDB in its
# object folder: it is put beside the library, where the copy looks.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(MSVC)
        set_target_properties(as3_parser PROPERTIES
            COMPILE_PDB_NAME as3_parser
            COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    add_custom_command(TARGET as3_parser POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE_DIR:as3_parser>/as3_parser.pdb
            ${PACKAGE_DIR}/lib/
    )
endif()

# Create a simple install script
file(WRITE ${PACKAGE_DIR}/README.md "# as3_parser\n\nActionScript-like Parser and Bytecode VM Library\n\n## Usage\n\nInclude the headers and link against the library:\n\n```cpp\n#include \"parser/parser.h\"\n#include \"bytecode/compiler.h\"\n#include \"bytecode/vm.h\"\n```\n")

# Print package location
message(STATUS "Package created at: ${PACKAGE_DIR}")
