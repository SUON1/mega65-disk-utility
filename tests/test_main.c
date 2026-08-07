#include "test.h"

#include <stdio.h>

int m65_test_failures = 0;

void m65_test_fail(const char *file, int line, const char *expression)
{
    (void)fprintf(stderr, "%s:%d: expectation failed: %s\n",
                  file, line, expression);
    ++m65_test_failures;
}

int main(void)
{
    test_bytes();
    test_cli();
    test_device();
    test_json();
    test_ufi();
    test_probe();
    if (m65_test_failures != 0) {
        (void)fprintf(stderr, "%d test expectation(s) failed\n", m65_test_failures);
        return 1;
    }
    (void)printf("all unit tests passed\n");
    return 0;
}
