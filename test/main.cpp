#include "test_runner.h"
#include <iostream>
#include <string>
#include <vector>

// Test suite modules are automatically registered via static initializers
// We just need to call the test runner

void printUsage() {
    std::cout << "Usage: as3_tests [suite_name]" << std::endl;
    std::cout << std::endl;
    std::cout << "Available test suites:" << std::endl;
    std::cout << "  parser      - Parser tests (52 tests)" << std::endl;
    std::cout << "  interpreter - Tree-walking interpreter tests" << std::endl;
    std::cout << "  bytecode    - Bytecode compiler tests" << std::endl;
    std::cout << "  vm          - Bytecode VM execution tests" << std::endl;
    std::cout << "  benchmark   - Performance benchmarks" << std::endl;
    std::cout << "  all         - Run all test suites (default)" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  as3_tests              # Run all tests" << std::endl;
    std::cout << "  as3_tests parser       # Run only parser tests" << std::endl;
    std::cout << "  as3_tests benchmark    # Run only benchmarks" << std::endl;
}

int main(int argc, char* argv[]) {
    std::string suite_filter = "all";

    if (argc > 1) {
        suite_filter = argv[1];
        if (suite_filter == "--help" || suite_filter == "-h") {
            printUsage();
            return 0;
        }
    }

    // Note: With the current simple test runner, we can't filter by suite name easily
    // For now, we'll just run all tests
    // TODO: Enhance test runner to support filtering

    if (suite_filter != "all") {
        std::cout << "Note: Suite filtering not yet implemented. Running all tests." << std::endl;
    }

    int result = as3test::TestRunner::getInstance().runAll();
    return result;
}
