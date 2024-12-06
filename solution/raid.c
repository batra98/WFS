
#include "raid.h"
#include "globals.h"
#include <stdio.h>
#include <string.h>

int get_raid_disk(int block_index) {
  DEBUG_LOG("Calculating RAID disk for block index %d in RAID mode %d.\n",
            block_index, sb.raid_mode);

  if (sb.raid_mode == RAID_0) {
    int disk_index = block_index % wfs_ctx.num_disks;
    DEBUG_LOG("RAID-0: Block %d maps to disk %d.\n", block_index, disk_index);
    return disk_index;
  } else if (sb.raid_mode == RAID_1) {
    DEBUG_LOG("RAID-1: Block %d always maps to disk 0 (primary disk).\n",
              block_index);
    return 0; // always return first disk
  }

  ERROR_LOG("Invalid RAID mode %d.\n", sb.raid_mode);
  return -1;
}

void replicate(const void *block, size_t block_offset, size_t block_size,
               int primary_disk_index) {
  DEBUG_LOG("Replicating block of size %zu at offset %zu from disk %d.\n",
            block_size, block_offset, primary_disk_index);

  for (int i = 0; i < wfs_ctx.num_disks; i++) {
    if (i == primary_disk_index) {
      DEBUG_LOG("Skipping primary disk %d for replication.\n", i);
      continue;
    }

    char *mirror_disk_mmap = (char *)wfs_ctx.disk_mmaps[i];
    if (!mirror_disk_mmap) {
      ERROR_LOG(
          "Disk %d mapping is invalid. Skipping replication for this disk.\n",
          i);
      continue;
    }

    memcpy(mirror_disk_mmap + block_offset, block, block_size);
    DEBUG_LOG("Replicated block at offset %zu to disk %d successfully.\n",
              block_offset, i);
  }
}

void initialize_raid(void **disk_mmaps, int num_disks, int raid_mode,
                     size_t *disk_sizes) {
  DEBUG_LOG("Initializing RAID with %d disks, mode %d.\n", num_disks,
            raid_mode);

  wfs_ctx.disk_mmaps = disk_mmaps;
  wfs_ctx.num_disks = num_disks;
  wfs_ctx.disk_sizes = disk_sizes;
  sb.raid_mode = raid_mode;

  DEBUG_LOG("RAID initialized: mode=%d, num_disks=%d.\n", sb.raid_mode,
            wfs_ctx.num_disks);
}
