#ifndef M65_TEST_H
#define M65_TEST_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern int m65_test_failures;

void m65_test_fail(const char *file, int line, const char *expression);
void test_bytes(void);
void test_cli(void);
void test_device(void);
void test_json(void);
void test_layout(void);
void test_probe(void);
void test_ufi(void);

#define EXPECT_TRUE(expression) \
    do { if (!(expression)) { m65_test_fail(__FILE__, __LINE__, #expression); } } while (0)
#define EXPECT_FALSE(expression) EXPECT_TRUE(!(expression))
#define EXPECT_EQ_U64(actual, expected) \
    do { if ((uint64_t)(actual) != (uint64_t)(expected)) { \
        m65_test_fail(__FILE__, __LINE__, #actual " == " #expected); } } while (0)
#define EXPECT_EQ_INT(actual, expected) \
    do { if ((int)(actual) != (int)(expected)) { \
        m65_test_fail(__FILE__, __LINE__, #actual " == " #expected); } } while (0)
#define EXPECT_MEMEQ(actual, expected, length) \
    do { if (memcmp((actual), (expected), (length)) != 0) { \
        m65_test_fail(__FILE__, __LINE__, #actual " byte-equals " #expected); } } while (0)
#define EXPECT_STREQ(actual, expected) \
    do { if (strcmp((actual), (expected)) != 0) { \
        m65_test_fail(__FILE__, __LINE__, #actual " string-equals " #expected); } } while (0)

#endif
