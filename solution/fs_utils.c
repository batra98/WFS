#include "wfs.h"
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define ALIGN_TO_BLOCK(offset)                                                 \
  (((offset) + BLOCK_SIZE - 1) / BLOCK_SIZE * BLOCK_SIZE)

size_t calculate_required_size(size_t inode_count, size_t data_block_count) {
  size_t sb_size = sizeof(struct wfs_sb);
  size_t i_bitmap_size = (inode_count + 7) / 8;
  size_t d_bitmap_size = (data_block_count + 7) / 8;
  size_t inode_table_size = inode_count * BLOCK_SIZE;
  size_t data_block_size = data_block_count * BLOCK_SIZE;

  size_t current_offset = sb_size;

  current_offset += i_bitmap_size;

  current_offset += d_bitmap_size;

  current_offset = ALIGN_TO_BLOCK(current_offset);
  current_offset += inode_table_size;

  current_offset = ALIGN_TO_BLOCK(current_offset);
  current_offset += data_block_size;

  return current_offset;
}

struct wfs_sb write_superblock(int fd, size_t inode_count,
                               size_t data_block_count) {
  size_t i_bitmap_size = (inode_count + 7) / 8;
  size_t d_bitmap_size = (data_block_count + 7) / 8;
  size_t inode_table_size = inode_count * BLOCK_SIZE;

  struct wfs_sb sb = {
      .num_inodes = inode_count,
      .num_data_blocks = data_block_count,
      .i_bitmap_ptr = sizeof(struct wfs_sb),
      .d_bitmap_ptr = (sizeof(struct wfs_sb) + i_bitmap_size),
      .i_blocks_ptr =
          ALIGN_TO_BLOCK(sizeof(struct wfs_sb) + i_bitmap_size + d_bitmap_size),
      .d_blocks_ptr = ALIGN_TO_BLOCK(sizeof(struct wfs_sb) + i_bitmap_size +
                                     d_bitmap_size + inode_table_size),
  };

  lseek(fd, 0, SEEK_SET);
  ssize_t bytes_written = write(fd, &sb, sizeof(struct wfs_sb));

  if (bytes_written != sizeof(struct wfs_sb)) {
    perror("Failed to write superblock");
    exit(EXIT_FAILURE);
  }

  return sb;
}

void write_bitmaps(int fd, size_t inode_count, size_t data_block_count,
                   struct wfs_sb *sb) {
  size_t i_bitmap_size = (inode_count + 7) / 8;
  size_t d_bitmap_size = (data_block_count + 7) / 8;

  char *bitmap = calloc(1, i_bitmap_size);
  bitmap[0] |= 1;

  lseek(fd, sb->i_bitmap_ptr, SEEK_SET);
  ssize_t bytes_written = write(fd, bitmap, i_bitmap_size);
  if (bytes_written != (ssize_t)i_bitmap_size) {
    perror("Failed to write inode bitmap");
    free(bitmap);
    exit(EXIT_FAILURE);
  }
  free(bitmap);

  bitmap = calloc(1, d_bitmap_size);
  lseek(fd, sb->d_bitmap_ptr, SEEK_SET);
  if (write(fd, bitmap, d_bitmap_size) != (ssize_t)d_bitmap_size) {
    perror("Failed to write data block bitmap");
    free(bitmap);
    exit(EXIT_FAILURE);
  }
  free(bitmap);
}

void write_inode(int fd, struct wfs_inode *inode, size_t inode_index,
                 struct wfs_sb *sb) {
  off_t inode_offset = sb->i_blocks_ptr + inode_index * BLOCK_SIZE;

  lseek(fd, inode_offset, SEEK_SET);
  write(fd, inode, sizeof(struct wfs_inode));
}

void write_root_inode(int fd, struct wfs_sb *sb) {
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

  write_inode(fd, &root, 0, sb);
}

int initialize_disk(const char *disk_file, size_t inode_count,
                    size_t data_block_count, size_t required_size) {
  int fd = open(disk_file, O_RDWR, 0644);
  if (fd < 0) {
    perror("Error opening disk file");
    return -1;
  }

  off_t disk_size = lseek(fd, 0, SEEK_END);
  printf("%ld %ld\n", disk_size, required_size);
  if (disk_size < required_size) {
    fprintf(stderr, "Disk %s is too small for the filesystem\n", disk_file);
    close(fd);
    return -1;
  }

  struct wfs_sb sb = write_superblock(fd, inode_count, data_block_count);
  write_bitmaps(fd, inode_count, data_block_count, &sb);
  write_root_inode(fd, &sb);

  close(fd);
  return 0;
}
