#ifndef RAID_H
#define RAID_H

#include <stddef.h>
int get_raid_disk(int block_index);
void replicate(const void *block, size_t block_offset, size_t block_size,
               int primary_disk_index);
void initialize_raid(void **disk_mmaps, int num_disks, int raid_mode,
                     size_t *disk_sizes);
#endif
