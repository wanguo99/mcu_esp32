#include "photo_file.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <unistd.h>

FILE *photo_file_create(const char *directory, uint32_t *sequence, char *path, size_t path_size)
{
    if (!directory || !sequence || *sequence == 0 || !path || path_size == 0) {
        errno = EINVAL;
        return NULL;
    }
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        int length = snprintf(path, path_size, "%s/photo_%05" PRIu32 ".jpg", directory, *sequence);
        if (length < 0 || (size_t)length >= path_size) {
            errno = ENAMETOOLONG;
            return NULL;
        }
        int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd >= 0) {
            FILE *file = fdopen(fd, "wb");
            if (!file) {
                int error = errno;
                close(fd);
                unlink(path);
                errno = error;
            }
            return file;
        }
        if (errno != EEXIST) return NULL;
        if (*sequence == UINT32_MAX) {
            errno = EOVERFLOW;
            return NULL;
        }
        ++*sequence;
    }
    errno = EEXIST;
    return NULL;
}
