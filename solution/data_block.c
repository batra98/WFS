
#include "globals.h"
#include "inode.h"
#include "raid.h"
#include "wfs.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void read_data_block(void *block, size_t block_index) {
  int disk_index = get_raid_disk(block_index / BLOCK_SIZE);
  if (disk_index < 0) {
    fprintf(stderr, "Error: Unable to get disk index for block %zu\n",
            block_index);
    return;
  }

  size_t block_offset = (block_index % BLOCK_SIZE) * BLOCK_SIZE;
  char *disk_mmap = (char *)wfs_ctx.disk_mmaps[disk_index];
  memcpy(block, disk_mmap + block_offset, BLOCK_SIZE);
}

void write_data_block(const void *block, size_t block_index) {
  int disk_index = get_raid_disk(block_index / BLOCK_SIZE);
  if (disk_index < 0) {
    fprintf(stderr, "Error: Unable to get disk index for block %zu\n",
            block_index);
    return;
  }

  size_t block_offset = (block_index % BLOCK_SIZE) * BLOCK_SIZE;
  char *disk_mmap = (char *)wfs_ctx.disk_mmaps[disk_index];
  memcpy(disk_mmap + block_offset, block, BLOCK_SIZE);

  if (sb.raid_mode == RAID_1)
    replicate(block, block_offset, BLOCK_SIZE, disk_index);
}

void read_data_block_bitmap(char *data_block_bitmap) {
  size_t data_bitmap_size = (sb.num_data_blocks + 7) / 8;
  int disk_index = get_raid_disk(0); // Always use the first disk for bitmap
  if (disk_index < 0) {
    fprintf(stderr, "Error: Unable to get disk index for bitmap\n");
    return;
  }

  char *disk_mmap = (char *)wfs_ctx.disk_mmaps[disk_index];
  memcpy(data_block_bitmap, disk_mmap + DATA_BITMAP_OFFSET, data_bitmap_size);
}

void write_data_block_bitmap(const char *data_block_bitmap) {
  size_t data_bitmap_size = (sb.num_data_blocks + 7) / 8;
  int disk_index = get_raid_disk(0); // Always use the first disk for bitmap
  if (disk_index < 0) {
    fprintf(stderr, "Error: Unable to get disk index for bitmap\n");
    return;
  }

  char *disk_mmap = (char *)wfs_ctx.disk_mmaps[disk_index];
  memcpy(disk_mmap + DATA_BITMAP_OFFSET, data_block_bitmap, data_bitmap_size);

  if (sb.raid_mode == RAID_1)
    replicate(data_block_bitmap, DATA_BITMAP_OFFSET, data_bitmap_size,
              disk_index);
}

int allocate_free_data_block() {
  size_t data_bitmap_size = (sb.num_data_blocks + 7) / 8;
  char data_block_bitmap[data_bitmap_size];

  read_data_block_bitmap(data_block_bitmap);

  for (int i = 0; i < sb.num_data_blocks; i++) {
    if (!(data_block_bitmap[i / 8] & (1 << (i % 8)))) {
      data_block_bitmap[i / 8] |= (1 << (i % 8));
      write_data_block_bitmap(data_block_bitmap);
      return i;
    }
  }

  return -ENOSPC;
}

void free_data_block(int block_index) {
  if (block_index < 0 || block_index >= sb.num_data_blocks) {
    fprintf(stderr, "Invalid data block index\n");
    return;
  }

  char data_block_bitmap[(sb.num_data_blocks + 7) / 8];
  read_data_block_bitmap(data_block_bitmap);

  data_block_bitmap[block_index / 8] &= ~(1 << (block_index % 8));
  write_data_block_bitmap(data_block_bitmap);
}

int add_dentry_to_parent(struct wfs_inode *parent_inode, int parent_inode_num,
                         const char *dirname, int inode_num) {
  struct wfs_dentry new_entry = {
      .num = inode_num,
  };
  strncpy(new_entry.name, dirname, MAX_NAME);

  for (int i = 0; i < N_BLOCKS; i++) {
    if (parent_inode->blocks[i] == -1) {
      // Allocate a new data block for the parent directory
      int block_num = allocate_free_data_block();
      if (block_num < 0) {
        return block_num;
      }
      parent_inode->blocks[i] = block_num;

      // Initialize the block
      struct wfs_dentry block[BLOCK_SIZE / sizeof(struct wfs_dentry)] = {0};
      block[0] = new_entry;

      write_data_block(block, block_num);
      break;
    }

    // Look for an empty dentry slot in the current block
    int disk_index = get_raid_disk(parent_inode->blocks[i] / BLOCK_SIZE);
    if (disk_index < 0) {
      return -EIO;
    }

    size_t block_offset =
        sb.d_blocks_ptr + parent_inode->blocks[i] * BLOCK_SIZE;
    struct wfs_dentry *dentries =
        (struct wfs_dentry *)((char *)wfs_ctx.disk_mmaps[disk_index] +
                              block_offset);

    for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
      if (dentries[j].num == -1) {
        dentries[j] = new_entry;
        break;
      }
    }
  }

  parent_inode->size += sizeof(struct wfs_dentry);
  parent_inode->nlinks++;
  write_inode(parent_inode, parent_inode_num);

  return 0;
}

int check_duplicate_dentry(const struct wfs_inode *parent_inode,
                           const char *dirname) {
  struct wfs_dentry *dentry;

  for (int i = 0; i < N_BLOCKS && parent_inode->blocks[i] != -1; i++) {
    int disk_index = get_raid_disk(parent_inode->blocks[i] / BLOCK_SIZE);
    if (disk_index < 0) {
      return -EIO;
    }

    size_t block_offset =
        sb.d_blocks_ptr + parent_inode->blocks[i] * BLOCK_SIZE;
    dentry = (struct wfs_dentry *)((char *)wfs_ctx.disk_mmaps[disk_index] +
                                   block_offset);

    for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
      if (dentry[j].num != -1 && strcmp(dentry[j].name, dirname) == 0) {
        return 0; // Duplicate found
      }
    }
  }
  return -ENOENT; // No duplicate
}
