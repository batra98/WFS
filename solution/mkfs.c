#include "fs_utils.h"
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  int raid_mode = -1, inode_count = 0, data_block_count = 0;
  char **disk_files = NULL;
  int disk_count = 0;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-r") == 0) {
      if (i + 1 < argc) {
        if (strcmp(argv[i + 1], "0") == 0) {
          raid_mode = 0;
        } else if (strcmp(argv[i + 1], "1") == 0) {
          raid_mode = 1;
        } else if (strcmp(argv[i + 1], "1v") == 0) {
          raid_mode = 2;
        } else {
          // fprintf(stderr, "Unsupported RAID mode: %s\n", argv[i + 1]);
          return 1;
        }
        i++;
      } else {
        // fprintf(stderr, "Missing argument for -r (RAID mode)\n");
        return 1;
      }
    } else if (strcmp(argv[i], "-d") == 0) {
      if (i + 1 < argc) {
        char **new_disk_files =
            realloc(disk_files, (disk_count + 1) * sizeof(char *));
        if (new_disk_files == NULL) {
          // fprintf(stderr, "Failed to allocate memory for disk files\n");
          free(disk_files);
          return 1;
        }
        disk_files = new_disk_files;
        disk_files[disk_count++] = argv[++i];
      } else {
        // fprintf(stderr, "Missing argument for -d (disk file)\n");
        free(disk_files);
        return 1;
      }
    } else if (strcmp(argv[i], "-i") == 0) {
      if (i + 1 < argc) {
        inode_count = atoi(argv[++i]);
        if (inode_count <= 0) {
          // fprintf(stderr, "Invalid inode count: %s\n", argv[i]);
          return 1;
        }
      } else {
        // fprintf(stderr, "Missing argument for -i (inode count)\n");
        return 1;
      }
    } else if (strcmp(argv[i], "-b") == 0) {
      if (i + 1 < argc) {
        data_block_count = atoi(argv[++i]);
        if (data_block_count <= 0) {
          // fprintf(stderr, "Invalid data block count: %s\n", argv[i]);
          return 1;
        }
      } else {
        // fprintf(stderr, "Missing argument for -b (data block count)\n");
        return 1;
      }
    }
  }

  if (raid_mode == -1) {
    // fprintf(stderr, "RAID mode (-r) must be specified (0, 1, or 1v)\n");
    return 1;
  }
  if (disk_count < 2) {
    return 1;
  }

  data_block_count = (data_block_count + 31) & ~31;
  inode_count = (inode_count + 31) & ~31;

  size_t required_size = calculate_required_size(inode_count, data_block_count);

  for (int i = 0; i < disk_count; i++) {
    if (initialize_disk(disk_files[i], inode_count, data_block_count,
                        required_size, raid_mode, i, disk_count) != 0) {
      // fprintf(stderr, "Failed to initialize disk: %s\n", disk_files[i]);
      return -1;
    }
  }

  // printf("Filesystem initialized successfully on %d disk(s).\n", disk_count);
  return 0;
}
