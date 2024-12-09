#ifndef GLOBAL_VARS_H
#define GLOBAL_VARS_H

#include "wfs.h"
#include <stdio.h>

#define RAID_0 0
#define RAID_1 1
#define RAID_1v 2

#define PRINT_SUPERBLOCK(sb)                                                   \
  do {                                                                         \
    DEBUG_LOG("Superblock Contents:\n");                                       \
    DEBUG_LOG("  Total Blocks: %ld\n", (sb).num_data_blocks);                  \
    DEBUG_LOG("  Inode Count: %ld\n", (sb).num_inodes);                        \
    DEBUG_LOG("  Data Blocks Pointer: %ld\n", (sb).d_blocks_ptr);              \
    DEBUG_LOG("  Inode Blocks Pointer: %ld\n", (sb).i_blocks_ptr);             \
    DEBUG_LOG("  Inode Bitmap Pointer: %ld\n", (sb).i_bitmap_ptr);             \
    DEBUG_LOG("  Data Bitmap Pointer: %ld\n", (sb).d_bitmap_ptr);              \
  } while (0)

#define PRINT_BITMAP(title, bitmap)                                            \
  do {                                                                         \
    DEBUG_LOG("%s\n", title);                                                  \
    for (int i = 0; i < BLOCK_SIZE; i++) {                                     \
      DEBUG_LOG("%02x ", (unsigned char)bitmap[i]);                            \
      if ((i + 1) % 16 == 0)                                                   \
        DEBUG_LOG("\n");                                                       \
    }                                                                          \
    DEBUG_LOG("\n");                                                           \
  } while (0)

#define DENTRY_OFFSET(block, index)                                            \
  (sb.d_blocks_ptr + (block) * BLOCK_SIZE + (index) * sizeof(struct wfs_dentry))
#define DATA_BLOCK_OFFSET(index) (sb.d_blocks_ptr + (index) * BLOCK_SIZE)
#define DATA_BITMAP_OFFSET sb.d_bitmap_ptr
#define INODE_OFFSET(index) (sb.i_blocks_ptr + (index) * BLOCK_SIZE)
#define INODE_BITMAP_OFFSET sb.i_bitmap_ptr

extern int debug;

#define DEBUG_LOG(fmt, ...)                                                    \
  do {                                                                         \
    if (debug)                                                                 \
      fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__);                     \
  } while (0)

#define ERROR_LOG(fmt, ...)                                                    \
  do {                                                                         \
    if (debug)                                                                 \
      fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__);                     \
  } while (0)

#define SET_BIT(bitmap, index) (bitmap[(index) / 8] |= (1 << ((index) % 8)))
#define IS_BIT_SET(bitmap, index) (bitmap[(index) / 8] & (1 << ((index) % 8)))
#define CLEAR_BIT(bitmap, index) (bitmap[(index) / 8] &= ~(1 << ((index) % 8)))

struct wfs_ctx {
  void **disk_mmaps;
  int num_disks;
  size_t *disk_sizes;
};

extern struct wfs_ctx wfs_ctx;
extern struct wfs_sb sb;

#endif // GLOBAL_VARS_H
