#ifndef RAID_H
#define RAID_H

int get_raid_disk(int block_index);
void initialize_raid(void **disk_mmaps, int num_disks, int raid_mode);

#endif
