#include <assert.h>

#include <cpu.h>

void assert_impl(const char *function, int line, bool condition, char *condtion_str) {
    if (!condition) {
        assert_debugf(ANSI_ASSERT_PANIC, "Condition %s failed!\n", function, line, condtion_str);
        _hcf();
    }
}
