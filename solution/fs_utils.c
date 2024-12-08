#include "globals.h"
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

static inline size_t calculate_bitmap_size(size_t count) {
  DEBUG_LOG("Calculating bitmap size for count: %zu", count);
  return (count + 7) / 8;
}

size_t calculate_required_size(size_t inode_count, size_t data_block_count) {
  DEBUG_LOG(
      "Calculating required size with inode_count: %zu, data_block_count: %zu",
      inode_count, data_block_count);

  size_t sb_size = sizeof(struct wfs_sb);
  size_t i_bitmap_size = calculate_bitmap_size(inode_count);
  size_t d_bitmap_size = calculate_bitmap_size(data_block_count);
  size_t inode_table_size = inode_count * BLOCK_SIZE;
  size_t data_block_size = data_block_count * BLOCK_SIZE;

  DEBUG_LOG(
      "Superblock size: %zu, inode bitmap size: %zu, data bitmap size: %zu",
      sb_size, i_bitmap_size, d_bitmap_size);

  size_t current_offset = sb_size + i_bitmap_size + d_bitmap_size;
  current_offset = ALIGN_TO_BLOCK(current_offset) + inode_table_size;
  current_offset = ALIGN_TO_BLOCK(current_offset) + data_block_size;

  DEBUG_LOG("Total required size: %zu", current_offset);
  return current_offset;
}

static inline uint64_t generate_disk_id(int disk_index) {
  DEBUG_LOG("Generating disk ID for disk_index: %d", disk_index);
  uint64_t disk_id = (uint64_t)time(NULL) ^ (disk_index + 1) ^ rand();
  DEBUG_LOG("Generated disk ID: %lu", disk_id);
  return disk_id;
}

struct wfs_sb write_superblock(int fd, size_t inode_count,
                               size_t data_block_count, int raid_mode,
                               int disk_index, int total_disks) {
  DEBUG_LOG("Writing superblock with inode_count: %zu, data_block_count: %zu, "
            "raid_mode: %d",
            inode_count, data_block_count, raid_mode);

  size_t i_bitmap_size = calculate_bitmap_size(inode_count);
  size_t d_bitmap_size = calculate_bitmap_size(data_block_count);
  size_t inode_table_size = inode_count * BLOCK_SIZE;

  struct wfs_sb sb = {
      .num_inodes = inode_count,
      .num_data_blocks = data_block_count,
      .i_bitmap_ptr = sizeof(struct wfs_sb),
      .d_bitmap_ptr = sizeof(struct wfs_sb) + i_bitmap_size,
      .i_blocks_ptr =
          ALIGN_TO_BLOCK(sizeof(struct wfs_sb) + i_bitmap_size + d_bitmap_size),
      .d_blocks_ptr = ALIGN_TO_BLOCK(sizeof(struct wfs_sb) + i_bitmap_size +
                                     d_bitmap_size + inode_table_size),
      .raid_mode = raid_mode,
      .disk_index = disk_index,
      .total_disks = total_disks,
      .disk_id = generate_disk_id(disk_index),
  };

  DEBUG_LOG("Superblock layout: inode_bitmap_ptr=%ld, data_bitmap_ptr=%ld, "
            "inode_blocks_ptr=%ld, data_blocks_ptr=%ld",
            sb.i_bitmap_ptr, sb.d_bitmap_ptr, sb.i_blocks_ptr, sb.d_blocks_ptr);

  lseek(fd, 0, SEEK_SET);
  ssize_t bytes_written = write(fd, &sb, sizeof(struct wfs_sb));
  if (bytes_written != sizeof(struct wfs_sb)) {
    ERROR_LOG("Failed to write superblock. Expected: %zu, Written: %zd",
              sizeof(struct wfs_sb), bytes_written);
    exit(EXIT_FAILURE);
  }

  DEBUG_LOG("Superblock written successfully. Disk ID: %lu", sb.disk_id);
  return sb;
}

void write_bitmaps(int fd, size_t inode_count, size_t data_block_count,
                   struct wfs_sb *sb) {
  size_t i_bitmap_size = calculate_bitmap_size(inode_count);
  size_t d_bitmap_size = calculate_bitmap_size(data_block_count);

  DEBUG_LOG("Writing inode bitmap at offset: %ld, size: %zu", sb->i_bitmap_ptr,
            i_bitmap_size);
  char *bitmap = calloc(1, i_bitmap_size);
  if (!bitmap) {
    ERROR_LOG("Memory allocation for inode bitmap failed");
    exit(EXIT_FAILURE);
  }
  bitmap[0] |= 1;

  lseek(fd, sb->i_bitmap_ptr, SEEK_SET);
  ssize_t bytes_written = write(fd, bitmap, i_bitmap_size);
  if (bytes_written != (ssize_t)i_bitmap_size) {
    ERROR_LOG("Failed to write inode bitmap");
    free(bitmap);
    exit(EXIT_FAILURE);
  }
  free(bitmap);

  DEBUG_LOG("Writing data block bitmap at offset: %ld, size: %zu",
            sb->d_bitmap_ptr, d_bitmap_size);
  bitmap = calloc(1, d_bitmap_size);
  lseek(fd, sb->d_bitmap_ptr, SEEK_SET);
  if (write(fd, bitmap, d_bitmap_size) != (ssize_t)d_bitmap_size) {
    ERROR_LOG("Failed to write data block bitmap");
    free(bitmap);
    exit(EXIT_FAILURE);
  }
  free(bitmap);
}

void write_inode_to_file(int fd, struct wfs_inode *inode, size_t inode_index,
                         struct wfs_sb *sb) {
  DEBUG_LOG("Writing inode %zu to file", inode_index);

  off_t inode_offset = sb->i_blocks_ptr + inode_index * BLOCK_SIZE;
  DEBUG_LOG("Inode offset: %ld", inode_offset);

  lseek(fd, inode_offset, SEEK_SET);
  if (write(fd, inode, sizeof(struct wfs_inode)) != sizeof(struct wfs_inode)) {
    ERROR_LOG("Failed to write inode %zu", inode_index);
    exit(EXIT_FAILURE);
  }
  DEBUG_LOG("Inode %zu written successfully", inode_index);
}

void write_root_inode(int fd, struct wfs_sb *sb) {
  DEBUG_LOG("Writing root inode");

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
  memset(root.blocks, -1, sizeof(root.blocks));
  write_inode_to_file(fd, &root, 0, sb);
  DEBUG_LOG("Root inode written successfully");
}

int initialize_disk(const char *disk_file, size_t inode_count,
                    size_t data_block_count, size_t required_size,
                    int raid_mode, int disk_index, int total_disks) {
  DEBUG_LOG("Initializing disk: %s", disk_file);

  int fd = open(disk_file, O_RDWR | O_CREAT, 0644);
  if (fd < 0) {
    ERROR_LOG("Error opening disk file %s", disk_file);
    return -1;
  }

  off_t disk_size = lseek(fd, 0, SEEK_END);
  if (disk_size < required_size) {
    ERROR_LOG("Disk %s is too small for the filesystem. Required: %zu, "
              "Available: %ld",
              disk_file, required_size, disk_size);
    close(fd);
    return -1;
  }
  DEBUG_LOG("Disk size validation successful");

  struct wfs_sb sb = write_superblock(fd, inode_count, data_block_count,
                                      raid_mode, disk_index, total_disks);
  write_bitmaps(fd, inode_count, data_block_count, &sb);
  write_root_inode(fd, &sb);

  close(fd);
  DEBUG_LOG("Disk %s initialized successfully", disk_file);
  return 0;
}

int split_path(const char *path, char *parent_path, char *dir_name) {
  DEBUG_LOG("Splitting path: %s", path);

  const char *last_slash = strrchr(path, '/');
  if (!last_slash) {
    strcpy(parent_path, "/");
    strncpy(dir_name, path, MAX_NAME);
    DEBUG_LOG("Path split result: parent_path=/, dir_name=%s", dir_name);
    return 0;
  }

  size_t parent_len = last_slash - path;
  strncpy(parent_path, path, parent_len);
  parent_path[parent_len] = '\0';
  strncpy(dir_name, last_slash + 1, MAX_NAME);

  DEBUG_LOG("Path split result: parent_path=%s, dir_name=%s", parent_path,
            dir_name);
  return 0;
}
