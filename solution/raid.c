#include "raid.h"
#include "globals.h"
#include <stdio.h>
#include <string.h>

int get_raid_disk(int block_index) {
  if (sb.raid_mode == RAID_0) {
    return block_index % wfs_ctx.num_disks;
  } else if (sb.raid_mode == RAID_1) {
    return 0; // always return first disk
  }
  return -1;
}

void replicate(const void *block, size_t block_offset, size_t block_size,
               int primary_disk_index) {
  for (int i = 0; i < wfs_ctx.num_disks; i++) {
    if (i == primary_disk_index) {
      continue; // Skip the primary disk
    }

    char *mirror_disk_mmap = (char *)wfs_ctx.disk_mmaps[i];
    if (!mirror_disk_mmap) {
      fprintf(stderr, "Error: Disk %d mapping is invalid. Skipping...\n", i);
      continue;
    }

    memcpy(mirror_disk_mmap, wfs_ctx.disk_mmaps[primary_disk_index],
           wfs_ctx.disk_sizes[i]);
    printf("Replicated block at offset %zu to disk %d.\n", block_offset, i);
  }
}

void initialize_raid(void **disk_mmaps, int num_disks, int raid_mode,
                     size_t *disk_sizes) {
  wfs_ctx.disk_mmaps = disk_mmaps;
  wfs_ctx.num_disks = num_disks;
  wfs_ctx.disk_sizes = disk_sizes;
  sb.raid_mode = raid_mode;
}
