#include "fs_utils.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  int raid_mode = -1, inode_count = 0, data_block_count = 0;
  char *disk_files[10];
  int disk_count = 0;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-r") == 0) {
      raid_mode = atoi(argv[++i]);
    } else if (strcmp(argv[i], "-d") == 0) {
      disk_files[disk_count++] = argv[++i];
    } else if (strcmp(argv[i], "-i") == 0) {
      inode_count = atoi(argv[++i]);
    } else if (strcmp(argv[i], "-b") == 0) {
      data_block_count = atoi(argv[++i]);
    }
  }

  if (raid_mode < 0 || disk_count == 0 || inode_count <= 0 ||
      data_block_count <= 0) {
    fprintf(stderr, "Usage: ./mkfs -r <raid_mode> -d <disk1> -d <disk2> ... -i "
                    "<inodes> -b <blocks>\n");
    return 1;
  }

  data_block_count = (data_block_count + 31) & ~31;

  int required_size = calculate_required_size(inode_count, data_block_count);

  for (int i = 0; i < disk_count; i++) {
    if (initialize_disk(disk_files[i], inode_count, data_block_count,
                        required_size) != 0) {
      fprintf(stderr, "Failed to initialize disk: %s\n", disk_files[i]);
      return 1;
    }
  }

  printf("Filesystem initialized successfully on %d disk(s).\n", disk_count);
  return 0;
}
