#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// Atomically create a numbered photo. Only name collisions are retried, up to
// 32 candidates. Other failures preserve errno and never truncate an old file.
FILE *photo_file_create(const char *directory, uint32_t *sequence, char *path, size_t path_size);
