#include "raid.h"
#include "globals.h"

int get_raid_disk(int block_index) {
  if (sb.raid_mode == RAID_0) {
    return block_index % wfs_ctx.num_disks;
  } else if (sb.raid_mode == RAID_1) {
    return 0; // always return first disk
  }
  return -1;
}

void initialize_raid(void **disk_mmaps, int num_disks, int raid_mode) {
  wfs_ctx.disk_mmaps = disk_mmaps;
  wfs_ctx.num_disks = num_disks;
  sb.raid_mode = raid_mode;
}
