#ifndef GLOBAL_VARS_H
#define GLOBAL_VARS_H

#include "wfs.h"

#define RAID_0 0
#define RAID_1 1
#define DENTRY_OFFSET(block, index)                                            \
  (sb.d_blocks_ptr + (block) * BLOCK_SIZE + (index) * sizeof(struct wfs_dentry))
#define DATA_BLOCK_OFFSET(index) (sb.d_blocks_ptr + (index) * BLOCK_SIZE)
#define DATA_BITMAP_OFFSET sb.d_bitmap_ptr
#define INODE_OFFSET(index) (sb.i_blocks_ptr + (index) * BLOCK_SIZE)
#define INODE_BITMAP_OFFSET sb.i_bitmap_ptr
#define DEBUG_LOG(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#define ERROR_LOG(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)
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
