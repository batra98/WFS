
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

  size_t block_offset = DATA_BLOCK_OFFSET(block_index);
  char *disk_mmap = (char *)wfs_ctx.disk_mmaps[disk_index];

  printf("Reading block %ld (offset: %zu)\n", block_index, block_offset);

  memcpy(block, disk_mmap + block_offset, BLOCK_SIZE);
}

void write_data_block(const void *block, size_t block_index) {
  int disk_index = get_raid_disk(block_index / BLOCK_SIZE);
  if (disk_index < 0) {
    fprintf(stderr, "Error: Unable to get disk index for block %zu\n",
            block_index);
    return;
  }

  size_t block_offset = DATA_BLOCK_OFFSET(block_index);
  char *disk_mmap = (char *)wfs_ctx.disk_mmaps[disk_index];
  memcpy(disk_mmap + block_offset, block, BLOCK_SIZE);

  printf("Writing block %ld (offset: %zu)\n", block_index, block_offset);

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
  int disk_index;
  int parent_block_num = -1;
  for (int i = 0; i < N_BLOCKS; i++) {
    if (parent_inode->blocks[i] == -1) {
      parent_block_num = allocate_free_data_block();
      if (parent_block_num < 0) {
        return parent_block_num;
      }
      parent_inode->blocks[i] = parent_block_num;
      printf("Allocated new data block for parent directory: parent_block_num "
             "= %d\n",
             parent_block_num);

      struct wfs_dentry new_block[BLOCK_SIZE / sizeof(struct wfs_dentry)];
      memset(new_block, -1, sizeof(new_block));
      new_block[0].num = inode_num;
      strncpy(new_block[0].name, dirname, MAX_NAME);

      write_data_block(new_block, parent_block_num);

      parent_inode->size += sizeof(struct wfs_dentry);
      parent_inode->nlinks++;
      write_inode(parent_inode, parent_inode_num);

      printf("Added new directory entry to newly allocated block: %s\n",
             dirname);
      return 0;
    }

    disk_index = get_raid_disk(parent_inode->blocks[i] / BLOCK_SIZE);
    if (disk_index < 0) {
      printf("Error: Unable to get disk index for parent directory block %d\n",
             i);
      return -EIO;
    }

    struct wfs_dentry *parent_dir_block =
        (struct wfs_dentry *)((char *)wfs_ctx.disk_mmaps[disk_index] +
                              DATA_BLOCK_OFFSET(parent_inode->blocks[i]));
    for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
      if (parent_dir_block[j].num == -1) {
        parent_dir_block[j].num = inode_num;
        strncpy(parent_dir_block[j].name, dirname, MAX_NAME);

        parent_inode->size += sizeof(struct wfs_dentry);
        parent_inode->nlinks++;
        write_data_block(parent_dir_block, parent_inode->blocks[i]);
        write_inode(parent_inode, parent_inode_num);

        printf("Added new directory entry to parent: %s\n", dirname);
        return 0;
      }
    }
  }

  printf("Error: No space left to add directory entry\n");
  return -ENOSPC;
}

int check_duplicate_dentry(const struct wfs_inode *parent_inode,
                           const char *dirname) {
  struct wfs_dentry *dentry;

  for (int i = 0; i < N_BLOCKS && parent_inode->blocks[i] != -1; i++) {
    int disk_index = get_raid_disk(parent_inode->blocks[i] / BLOCK_SIZE);
    if (disk_index < 0) {
      return -EIO;
    }

    size_t block_offset = DATA_BLOCK_OFFSET(parent_inode->blocks[i]);
    dentry = (struct wfs_dentry *)((char *)wfs_ctx.disk_mmaps[disk_index] +
                                   block_offset);

    for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
      if (dentry[j].num != -1 && strcmp(dentry[j].name, dirname) == 0) {
        return 0;
      }
    }
  }
  return -ENOENT;
}

int allocate_direct_block(struct wfs_inode *inode, size_t block_index) {
  if (inode->blocks[block_index] == -1) {
    inode->blocks[block_index] = allocate_free_data_block();
    if (inode->blocks[block_index] < 0) {
      printf("Failed to allocate data block for direct block %zu\n",
             block_index);
      return -EIO;
    }
  }
  return inode->blocks[block_index];
}

int allocate_indirect_block(struct wfs_inode *inode, size_t block_index,
                            char *block_buffer) {
  int N_DIRECT = N_BLOCKS - 1;
  if (inode->blocks[N_DIRECT] == -1) {
    inode->blocks[N_DIRECT] = allocate_free_data_block();
    if (inode->blocks[N_DIRECT] < 0) {
      printf("Failed to allocate indirect block\n");
      return -EIO;
    }

    memset(block_buffer, -1, BLOCK_SIZE);
    write_data_block(block_buffer, inode->blocks[N_DIRECT]);
  }

  read_data_block(block_buffer, inode->blocks[N_DIRECT]);
  int *indirect_blocks = (int *)block_buffer;

  size_t indirect_index = block_index - N_DIRECT;
  if (indirect_index >= BLOCK_SIZE / sizeof(int)) {
    printf("Indirect index out of bounds: %zu\n", indirect_index);
    return -EIO;
  }

  if (indirect_blocks[indirect_index] == -1) {
    indirect_blocks[indirect_index] = allocate_free_data_block();
    if (indirect_blocks[indirect_index] < 0) {
      printf("Failed to allocate data block for indirect entry %zu\n",
             indirect_index);
      return -EIO;
    }

    write_data_block(block_buffer, inode->blocks[N_DIRECT]);
  }

  return indirect_blocks[indirect_index];
}

void update_inode_size(struct wfs_inode *inode, size_t inode_num,
                       off_t new_size) {
  if (new_size > inode->size) {
    inode->size = new_size;
    write_inode(inode, inode_num);
  }
}
