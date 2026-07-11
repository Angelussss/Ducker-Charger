#ifndef FRAMEWORK_H
#define FRAMEWORK_H

#include <stdio.h>

static int _tests_run    = 0;
static int _tests_failed = 0;

#define ASSERT(cond) do { \
    _tests_run++; \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #cond); \
        _tests_failed++; \
    } \
} while (0)

#define TEST_RESULT() do { \
    if (_tests_failed == 0) \
        printf("%s: ALL PASSED (%d)\n", __FILE__, _tests_run); \
    else \
        fprintf(stderr, "%s: %d/%d FAILED\n", __FILE__, _tests_failed, _tests_run); \
    return (_tests_failed > 0) ? 1 : 0; \
} while (0)

#endif // FRAMEWORK_H
