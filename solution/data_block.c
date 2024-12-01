
#include "globals.h"
#include "wfs.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DATA_BLOCK_OFFSET(index) (sb.d_blocks_ptr + (index) * BLOCK_SIZE)
#define DATA_BITMAP_OFFSET sb.d_bitmap_ptr
#define DENTRY_OFFSET(block, index)                                            \
  ((block) * BLOCK_SIZE + (index) * sizeof(struct wfs_dentry))

void read_data_block(void *block, size_t block_index) {
  off_t block_offset = DATA_BLOCK_OFFSET(block_index);
  memcpy(block, (char *)disk_mmap + block_offset, BLOCK_SIZE);
}

void write_data_block(const void *block, size_t block_index) {
  off_t block_offset = DATA_BLOCK_OFFSET(block_index);
  memcpy((char *)disk_mmap + block_offset, block, BLOCK_SIZE);
}

void read_data_block_bitmap(char *data_block_bitmap) {
  size_t data_bitmap_size = (sb.num_data_blocks + 7) / 8;
  memcpy(data_block_bitmap, (char *)disk_mmap + DATA_BITMAP_OFFSET,
         data_bitmap_size);
}

void write_data_block_bitmap(const char *data_block_bitmap) {
  size_t data_bitmap_size = (sb.num_data_blocks + 7) / 8;
  memcpy((char *)disk_mmap + DATA_BITMAP_OFFSET, data_block_bitmap,
         data_bitmap_size);
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

void add_dentry_to_dir(struct wfs_inode *parent_inode,
                       struct wfs_dentry *dentry) {
  for (int i = 0; i < N_BLOCKS; i++) {
    if (parent_inode->blocks[i] == 0) {
      continue;
    }

    size_t num_entries = BLOCK_SIZE / sizeof(struct wfs_dentry);
    for (size_t j = 0; j < num_entries; j++) {
      struct wfs_dentry *entry =
          (struct wfs_dentry *)((char *)disk_mmap +
                                DENTRY_OFFSET(parent_inode->blocks[i], j));
      if (entry->num == -1) {
        memcpy(entry, dentry, sizeof(struct wfs_dentry));
        parent_inode->size += sizeof(struct wfs_dentry);
        return;
      }
    }
  }

  if (parent_inode->size / BLOCK_SIZE >= N_BLOCKS) {
    fprintf(stderr, "Directory has reached its block limit\n");
    return;
  }

  int new_block_index = allocate_free_data_block();
  if (new_block_index == -ENOSPC) {
    fprintf(stderr, "No free data blocks available\n");
    return;
  }

  for (int i = 0; i < N_BLOCKS; i++) {
    if (parent_inode->blocks[i] == 0) {
      parent_inode->blocks[i] = new_block_index;
      break;
    }
  }

  struct wfs_dentry *new_block =
      (struct wfs_dentry *)((char *)disk_mmap +
                            DATA_BLOCK_OFFSET(new_block_index));
  memset(new_block, -1, BLOCK_SIZE);
  memcpy(new_block, dentry, sizeof(struct wfs_dentry));

  parent_inode->size += sizeof(struct wfs_dentry);
}
