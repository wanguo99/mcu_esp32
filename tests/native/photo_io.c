#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/types.h>

static int s_error, s_calls;
int __real_open(const char *path, int flags, ...);
int __wrap_open(const char *path, int flags, ...)
{
    ++s_calls;
    if (s_error) { errno = s_error; return -1; }
    va_list args;
    va_start(args, flags);
    mode_t mode = (mode_t)va_arg(args, int);
    va_end(args);
    return __real_open(path, flags, mode);
}
void test_photo_open_error(int error) { s_error = error; s_calls = 0; }
int test_photo_open_calls(void) { return s_calls; }
