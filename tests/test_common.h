/**
 * @file test_common.h
 * @brief Unified test framework for PipeWine ASIO driver
 *
 * This header provides a simple test framework for the PipeWine test suite.
 * Include this header in any test file to get access to test assertion macros
 * and common utilities.
 */

#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* Test result tracking */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/* Test assertion macros */

#define TEST_ASSERT(condition, message) do { \
    tests_run++; \
    if (!(condition)) { \
        printf("FAIL: %s (line %d): %s\n", __func__, __LINE__, message); \
        tests_failed++; \
    } else { \
        tests_passed++; \
        printf("PASS: %s\n", message); \
    } \
} while(0)

#define TEST_ASSERT_EQ(expected, actual, message) do { \
    tests_run++; \
    if ((expected) != (actual)) { \
        printf("FAIL: %s (line %d): %s (expected %d, got %d)\n", \
               __func__, __LINE__, message, (int)(expected), (int)(actual)); \
        tests_failed++; \
    } else { \
        tests_passed++; \
        printf("PASS: %s\n", message); \
    } \
} while(0)

#define TEST_ASSERT_STR_EQ(expected, actual, message) do { \
    tests_run++; \
    if (strcmp((expected), (actual)) != 0) { \
        printf("FAIL: %s (line %d): %s (expected '%s', got '%s')\n", \
               __func__, __LINE__, message, expected, actual); \
        tests_failed++; \
    } else { \
        tests_passed++; \
        printf("PASS: %s\n", message); \
    } \
} while(0)

#define TEST_ASSERT_NOT_NULL(ptr, message) do { \
    tests_run++; \
    if ((ptr) == NULL) { \
        printf("FAIL: %s (line %d): %s (expected non-NULL)\n", \
               __func__, __LINE__, message); \
        tests_failed++; \
    } else { \
        tests_passed++; \
        printf("PASS: %s\n", message); \
    } \
} while(0)

#define TEST_ASSERT_NULL(ptr, message) do { \
    tests_run++; \
    if ((ptr) != NULL) { \
        printf("FAIL: %s (line %d): %s (expected NULL)\n", \
               __func__, __LINE__, message); \
        tests_failed++; \
    } else { \
        tests_passed++; \
        printf("PASS: %s\n", message); \
    } \
} while(0)

/* Test section markers */

#define TEST_BEGIN(name) \
    printf("\n=== Test: %s ===\n", name);

#define TEST_SECTION(name) \
    printf("\n--- %s ---\n", name);

#define TEST_END() do { \
    printf("\n"); \
    printf("========================================\n"); \
    printf("Results: %d/%d passed (%d failed)\n", \
           tests_passed, tests_run, tests_failed); \
    printf("========================================\n"); \
    return tests_failed > 0 ? 1 : 0; \
} while(0)

/* Skip test with message */
#define TEST_SKIP(reason) do { \
    printf("SKIP: %s\n", reason); \
    return 0; \
} while(0)

/* Common test context for ASIO tests */
typedef struct {
    void *driver;
    bool initialized;
    int num_input_channels;
    int num_output_channels;
} TestContext;

/* Reset test counters (for multiple test files in one run) */
static inline void test_reset_counters(void) {
    tests_run = 0;
    tests_passed = 0;
    tests_failed = 0;
}

/* Print test summary without returning */
static inline void test_print_summary(void) {
    printf("\nSummary: %d/%d tests passed (%d failed)\n",
           tests_passed, tests_run, tests_failed);
}

/* Check if all tests passed */
static inline bool test_all_passed(void) {
    return tests_failed == 0;
}

#endif /* TEST_COMMON_H */
