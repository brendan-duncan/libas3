#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <functional>

namespace as3test {

// Test result
struct TestResult {
    std::string name;
    bool passed;
    std::string error;
};

// Test suite
class TestSuite {
public:
    TestSuite(const std::string& name)
        : suiteName(name) {
    }

    void addTest(const std::string& name, std::function<void()> test) {
        tests.push_back({name, test});
    }

    std::vector<TestResult> run() {
        std::vector<TestResult> results;

        std::cout << "\n========================================" << std::endl;
        std::cout << "Test Suite: " << suiteName << std::endl;
        std::cout << "========================================" << std::endl;

        for (const auto& test : tests) {
            TestResult result;
            result.name = test.first;
            result.passed = false;

            try {
                test.second();
                result.passed = true;
                std::cout << "[PASS] " << test.first << std::endl;
            } catch (const std::exception& e) {
                result.error = e.what();
                std::cout << "[FAIL] " << test.first << ": " << e.what() << std::endl;
            } catch (...) {
                result.error = "Unknown error";
                std::cout << "[FAIL] " << test.first << ": Unknown error" << std::endl;
            }

            results.push_back(result);
        }

        return results;
    }

    const std::string& getName() const { return suiteName; }

private:
    std::string suiteName;
    std::vector<std::pair<std::string, std::function<void()>>> tests;
};

// Test runner
class TestRunner {
public:
    static TestRunner& getInstance() {
        static TestRunner instance;
        return instance;
    }

    void addSuite(TestSuite* suite) {
        suites.push_back(suite);
    }

    int runAll() {
        std::cout << "\n===== ActionScript 3 Test Suite =====" << std::endl;
        std::cout << "Running " << suites.size() << " test suites" << std::endl;

        int totalTests = 0;
        int totalPassed = 0;
        int totalFailed = 0;

        for (auto* suite : suites) {
            auto results = suite->run();

            int passed = 0;
            int failed = 0;
            for (const auto& result : results) {
                if (result.passed) {
                    passed++;
                } else {
                    failed++;
                }
            }

            totalTests += results.size();
            totalPassed += passed;
            totalFailed += failed;

            std::cout << "Suite: " << suite->getName()
                      << " - " << passed << "/" << results.size() << " passed" << std::endl;
        }

        std::cout << "\n===== Summary =====" << std::endl;
        std::cout << "Total Tests: " << totalTests << std::endl;
        std::cout << "Passed: " << totalPassed << std::endl;
        std::cout << "Failed: " << totalFailed << std::endl;

        if (totalFailed == 0) {
            std::cout << "\nAll tests passed!" << std::endl;
            return 0;
        } else {
            std::cout << "\nSome tests failed" << std::endl;
            return 1;
        }
    }

private:
    TestRunner() = default;
    std::vector<TestSuite*> suites;
};

// Helper macro for test registration
#define TEST_SUITE(name) \
    static as3test::TestSuite suite_##name(#name); \
    static struct SuiteRegistrar_##name { \
        SuiteRegistrar_##name() { \
            as3test::TestRunner::getInstance().addSuite(&suite_##name); \
        } \
    } registrar_##name;

} // namespace as3test
