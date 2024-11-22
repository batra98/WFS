#include "fs_utils.h"
#include "wfs.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int calculate_required_size(size_t inode_count, size_t data_block_count) {
  size_t sb_size = sizeof(struct wfs_sb);
  size_t i_bitmap_size = (inode_count + 7) / 8;
  size_t d_bitmap_size = (data_block_count + 7) / 8;
  size_t inode_table_size = inode_count * sizeof(struct wfs_inode);
  size_t data_block_size = data_block_count * BLOCK_SIZE;

  return sb_size + i_bitmap_size + d_bitmap_size + inode_table_size +
         data_block_size;
}

void write_superblock(int fd, size_t inode_count, size_t data_block_count) {
  struct wfs_sb sb = {
      .num_inodes = inode_count,
      .num_data_blocks = data_block_count,
      .i_bitmap_ptr = sizeof(struct wfs_sb),
      .d_bitmap_ptr = sizeof(struct wfs_sb) + (inode_count + 7) / 8,
      .i_blocks_ptr = sizeof(struct wfs_sb) + (inode_count + 7) / 8 +
                      (data_block_count + 7) / 8,
      .d_blocks_ptr = sizeof(struct wfs_sb) + (inode_count + 7) / 8 +
                      (data_block_count + 7) / 8 +
                      inode_count * sizeof(struct wfs_inode),
  };

  lseek(fd, 0, SEEK_SET);
  write(fd, &sb, sizeof(struct wfs_sb));
}

void write_bitmaps(int fd, size_t inode_count, size_t data_block_count) {
  size_t i_bitmap_size = (inode_count + 7) / 8;
  size_t d_bitmap_size = (data_block_count + 7) / 8;

  char *bitmap = calloc(1, i_bitmap_size);
  lseek(fd, sizeof(struct wfs_sb), SEEK_SET);
  write(fd, bitmap, i_bitmap_size);
  free(bitmap);

  bitmap = calloc(1, d_bitmap_size);
  write(fd, bitmap, d_bitmap_size);
  free(bitmap);
}

void write_root_inode(int fd, size_t inode_count) {
  struct wfs_inode root = {
      .num = 0,
      .mode = S_IFDIR | 0755,
      .uid = getuid(),
      .gid = getgid(),
      .size = 0,
      .nlinks = 2,
      .atim = time(NULL),
      .mtim = time(NULL),
      .ctim = time(NULL),
  };

  lseek(fd,
        sizeof(struct wfs_sb) + (inode_count + 7) / 8 + (inode_count + 7) / 8,
        SEEK_SET);
  write(fd, &root, sizeof(struct wfs_inode));
}

int initialize_disk(const char *disk_file, size_t inode_count,
                    size_t data_block_count, int required_size) {
  int fd = open(disk_file, O_RDWR | O_CREAT, 0644);
  if (fd < 0) {
    perror("Error opening disk file");
    return -1;
  }

  off_t disk_size = lseek(fd, 0, SEEK_END);
  if (disk_size < required_size) {
    fprintf(stderr, "Disk %s is too small for the filesystem\n", disk_file);
    close(fd);
    return -1;
  }

  write_superblock(fd, inode_count, data_block_count);
  write_bitmaps(fd, inode_count, data_block_count);
  write_root_inode(fd, inode_count);

  close(fd);
  return 0;
}
