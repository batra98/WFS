
#include "raid.h"
#include "globals.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int get_raid_disk(int block_index, int *disk_index) {
  DEBUG_LOG("Calculating RAID disk for block index %d in RAID mode %d.\n",
            block_index, sb.raid_mode);

  if (sb.raid_mode == RAID_0) {
    *disk_index = block_index % wfs_ctx.num_disks;
    DEBUG_LOG("RAID-0: Block %d maps to disk %d.\n", block_index, *disk_index);
  } else if (sb.raid_mode == RAID_1 || sb.raid_mode == RAID_1v) {
    DEBUG_LOG("RAID-1: Block %d always maps to disk 0 (primary disk).\n",
              block_index);
    *disk_index = 0;
  }

  return block_index / wfs_ctx.num_disks;
}

int get_majority_block(char *block, size_t block_offset) {
  int num_disks = wfs_ctx.num_disks;
  char **block_data = malloc(num_disks * sizeof(char *));
  int *votes = calloc(num_disks, sizeof(int));

  if (!block_data || !votes) {
    ERROR_LOG("Memory allocation failed for majority block computation\n");
    free(block_data);
    free(votes);
    return -1;
  }

  for (int i = 0; i < num_disks; i++) {
    block_data[i] = malloc(BLOCK_SIZE);
    if (!block_data[i]) {
      ERROR_LOG("Memory allocation failed for block data\n");
      for (int j = 0; j < i; j++) {
        free(block_data[j]);
      }
      free(block_data);
      free(votes);
      return -1;
    }
  }

  for (int i = 0; i < num_disks; i++) {
    memcpy(block_data[i], (char *)wfs_ctx.disk_mmaps[i] + block_offset,
           BLOCK_SIZE);
  }

  for (int i = 0; i < num_disks; i++) {
    for (int j = i + 1; j < num_disks; j++) {
      if (memcmp(block_data[i], block_data[j], BLOCK_SIZE) == 0) {
        votes[i]++;
        votes[j]++;
      }
    }
  }

  int majority_disk_index = -1;
  int max_votes = -1;
  for (int i = 0; i < num_disks; i++) {
    if (votes[i] > max_votes) {
      max_votes = votes[i];
      majority_disk_index = i;
    }
  }

  if (majority_disk_index < 0) {
    ERROR_LOG("Unable to find majority value for block at offset %zu\n",
              block_offset);
    for (int i = 0; i < num_disks; i++) {
      free(block_data[i]);
    }
    free(block_data);
    free(votes);
    return -1;
  }

  memcpy(block, block_data[majority_disk_index], BLOCK_SIZE);

  for (int i = 0; i < num_disks; i++) {
    free(block_data[i]);
  }
  free(block_data);
  free(votes);

  return 0;
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
