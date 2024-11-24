#ifndef FS_UTILS_H
#define FS_UTILS_H

#include "wfs.h"
#include <stddef.h>

size_t calculate_required_size(size_t inode_count, size_t data_block_count);

int initialize_disk(const char *disk_file, size_t inode_count,
                    size_t data_block_count, int required_size);

#endif // FS_UTILS_H
