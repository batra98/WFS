#include "globals.h"
#include "inode.h"
#include "raid.h"
#include "wfs.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ALLOCATE_AND_WRITE_DATA_BLOCK(block_buffer, block_num)                 \
  do {                                                                         \
    memset(block_buffer, -1, BLOCK_SIZE);                                      \
    if (write_data_block(block_buffer, block_num) < 0) {                       \
      ERROR_LOG("Failed to write data block");                                 \
      return -EIO;                                                             \
    }                                                                          \
  } while (0)

void read_data_block(void *block, size_t block_index) {
  int disk_index;
  block_index = get_raid_disk(block_index, &disk_index);
  if (disk_index < 0) {
    ERROR_LOG("Unable to get disk index for block %zu\n", block_index);
    return;
  }

  size_t block_offset = DATA_BLOCK_OFFSET(block_index);
  memcpy(block, (char *)wfs_ctx.disk_mmaps[disk_index] + block_offset,
         BLOCK_SIZE);
  DEBUG_LOG("Read block %zu (offset: %zu) from disk %d\n", block_index,
            block_offset, disk_index);
}

void write_data_block(const void *block, size_t block_index) {
  int disk_index;
  block_index = get_raid_disk(block_index, &disk_index);
  if (disk_index < 0) {
    ERROR_LOG("Unable to get disk index for block %zu\n", block_index);
    return;
  }

  size_t block_offset = DATA_BLOCK_OFFSET(block_index);
  memcpy((char *)wfs_ctx.disk_mmaps[disk_index] + block_offset, block,
         BLOCK_SIZE);
  DEBUG_LOG("Wrote block %zu (offset: %zu) to disk %d\n", block_index,
            block_offset, disk_index);

  if (sb.raid_mode == RAID_1) {
    replicate(block, block_offset, BLOCK_SIZE, disk_index);
  }
}

void read_data_block_bitmap(char *data_block_bitmap, int disk_index) {

  size_t data_bitmap_size = (sb.num_data_blocks + 7) / 8;
  if (disk_index < 0) {
    ERROR_LOG("Unable to get disk index for data block bitmap\n");
    return;
  }

  memcpy(data_block_bitmap,
         (char *)wfs_ctx.disk_mmaps[disk_index] + DATA_BITMAP_OFFSET,
         data_bitmap_size);
  DEBUG_LOG("Read data block bitmap from disk %d\n", disk_index);
}

void write_data_block_bitmap(const char *data_block_bitmap, int disk_index) {
  size_t data_bitmap_size = (sb.num_data_blocks + 7) / 8;
  if (disk_index < 0) {
    ERROR_LOG("Unable to get disk index for data block bitmap\n");
    return;
  }

  memcpy((char *)wfs_ctx.disk_mmaps[disk_index] + DATA_BITMAP_OFFSET,
         data_block_bitmap, data_bitmap_size);
  DEBUG_LOG("Wrote data block bitmap to disk %d\n", disk_index);

  if (sb.raid_mode == RAID_1) {
    replicate(data_block_bitmap, DATA_BITMAP_OFFSET, data_bitmap_size,
              disk_index);
  }
}

int allocate_free_data_block() {
  size_t data_bitmap_size = (sb.num_data_blocks + 7) / 8;
  char data_block_bitmap[data_bitmap_size];

  if (sb.raid_mode == RAID_0) {
    read_data_block_bitmap(data_block_bitmap, 0);

    for (int i = 0; i < sb.num_data_blocks; i++) {
      if (!IS_BIT_SET(data_block_bitmap, i)) {
        SET_BIT(data_block_bitmap, i);
        write_data_block_bitmap(data_block_bitmap, 0);
        DEBUG_LOG("Allocated data block %d\n", i);
        return i;
      }
    }
  } else if (sb.raid_mode == RAID_1) {

    for (int i = 0; i < sb.num_data_blocks; i++) {
      for (int j = 0; j < wfs_ctx.num_disks; j++) {
        read_data_block_bitmap(data_block_bitmap, j);

        if (!IS_BIT_SET(data_block_bitmap, i)) {
          SET_BIT(data_block_bitmap, i);
          write_data_block_bitmap(data_block_bitmap, j);
          DEBUG_LOG("Allocated data block %d on disk %d", i, j);
          return i * wfs_ctx.num_disks + j;
        }
      }
    }
  }

  ERROR_LOG("No free data blocks available\n");
  return -ENOSPC;
}

void free_data_block(int block_index) {
  int disk_index;
  block_index = get_raid_disk(block_index, &disk_index);
  if (block_index < 0 || block_index >= sb.num_data_blocks) {
    ERROR_LOG("Invalid data block index %d\n", block_index);
    return;
  }

  char data_block_bitmap[(sb.num_data_blocks + 7) / 8];
  read_data_block_bitmap(data_block_bitmap, disk_index);

  CLEAR_BIT(data_block_bitmap, block_index);
  write_data_block_bitmap(data_block_bitmap, disk_index);
  DEBUG_LOG("Freed data block %d\n", block_index);
}

void free_direct_data_blocks(struct wfs_inode *inode) {
  for (int i = 0; i < N_BLOCKS - 1; i++) {
    if (inode->blocks[i] != -1) {
      free_data_block(inode->blocks[i]);
      inode->blocks[i] = -1;
    }
  }
}

void free_indirect_data_block(struct wfs_inode *inode) {
  if (inode->blocks[N_BLOCKS - 1] != -1) {
    char block_buffer[BLOCK_SIZE];
    read_data_block(block_buffer, inode->blocks[N_BLOCKS - 1]);

    int *indirect_blocks = (int *)block_buffer;

    for (int i = 0; i < BLOCK_SIZE / sizeof(int); i++) {
      if (indirect_blocks[i] != -1) {
        free_data_block(indirect_blocks[i]);
        indirect_blocks[i] = -1;
      }
    }

    free_data_block(inode->blocks[N_BLOCKS - 1]);
    inode->blocks[N_BLOCKS - 1] = -1;
  }
}

int add_dentry_to_parent(struct wfs_inode *parent_inode, int parent_inode_num,
                         const char *dirname, int inode_num) {
  for (int i = 0; i < N_BLOCKS; i++) {
    if (parent_inode->blocks[i] == -1) {
      int new_block = allocate_free_data_block();
      if (new_block < 0)
        return new_block;

      parent_inode->blocks[i] = new_block;
      DEBUG_LOG("Allocated new data block %d for parent inode %d\n", new_block,
                parent_inode_num);

      struct wfs_dentry
          new_block_content[BLOCK_SIZE / sizeof(struct wfs_dentry)];
      memset(new_block_content, -1, sizeof(new_block_content));

      new_block_content[0].num = inode_num;
      strncpy(new_block_content[0].name, dirname, MAX_NAME);

      write_data_block(new_block_content, new_block);

      parent_inode->size += sizeof(struct wfs_dentry);
      parent_inode->nlinks++;
      write_inode(parent_inode, parent_inode_num);
      DEBUG_LOG("Added dentry %s (inode %d) to parent %d in a new block\n",
                dirname, inode_num, parent_inode_num);
      return 0;
    }

    struct wfs_dentry block_content[BLOCK_SIZE / sizeof(struct wfs_dentry)];
    read_data_block(block_content, parent_inode->blocks[i]);

    for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
      if (block_content[j].num == -1) {
        block_content[j].num = inode_num;
        strncpy(block_content[j].name, dirname, MAX_NAME);

        write_data_block(block_content, parent_inode->blocks[i]);

        parent_inode->size += sizeof(struct wfs_dentry);
        parent_inode->nlinks++;
        write_inode(parent_inode, parent_inode_num);
        DEBUG_LOG("Added dentry %s (inode %d) to parent %d\n", dirname,
                  inode_num, parent_inode_num);
        return 0;
      }
    }
  }

  ERROR_LOG("No space left to add directory entry in parent inode %d\n",
            parent_inode_num);
  return -ENOSPC;
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

int check_duplicate_dentry(const struct wfs_inode *parent_inode,
                           const char *dirname) {
  struct wfs_dentry *dentry;

  for (int i = 0; i < N_BLOCKS && parent_inode->blocks[i] != -1; i++) {
    DEBUG_LOG("Checking block for duplicate directory entry");

    int block_index = parent_inode->blocks[i];

    int disk_index;
    block_index = get_raid_disk(block_index, &disk_index);

    if (disk_index < 0) {
      ERROR_LOG("Failed to get disk index");
      return -EIO;
    }

    size_t block_offset = DATA_BLOCK_OFFSET(block_index);
    dentry = (struct wfs_dentry *)((char *)wfs_ctx.disk_mmaps[disk_index] +
                                   block_offset);

    for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
      if (dentry[j].num != -1 && strcmp(dentry[j].name, dirname) == 0) {
        DEBUG_LOG("Found duplicate dentry");
        return 0; // Found duplicate entry
      }
    }
  }
  DEBUG_LOG("No duplicate dentry found");
  return -ENOENT; // No duplicate found
}

int allocate_indirect_block(struct wfs_inode *inode, size_t block_index,
                            char *block_buffer) {
  int N_DIRECT = N_BLOCKS - 1;

  if (inode->blocks[N_DIRECT] == -1) {
    DEBUG_LOG("Indirect block not allocated, allocating now");
    inode->blocks[N_DIRECT] = allocate_free_data_block();
    if (inode->blocks[N_DIRECT] < 0) {
      ERROR_LOG("Failed to allocate indirect block");
      return -EIO;
    }

    memset(block_buffer, -1, BLOCK_SIZE);
    write_data_block(block_buffer, inode->blocks[N_DIRECT]);
  }

  read_data_block(block_buffer, inode->blocks[N_DIRECT]);
  int *indirect_blocks = (int *)block_buffer;

  size_t indirect_index = block_index - N_DIRECT;
  if (indirect_index >= BLOCK_SIZE / sizeof(int)) {
    ERROR_LOG("Indirect index out of bounds");
    return -EIO;
  }

  if (indirect_blocks[indirect_index] == -1) {
    DEBUG_LOG("Indirect entry not allocated, performing lazy allocation");
    return -1; // Lazy allocation
  }

  return indirect_blocks[indirect_index];
}

void update_inode_size(struct wfs_inode *inode, size_t inode_num,
                       off_t new_size) {
  if (new_size > inode->size) {
    DEBUG_LOG("Updating inode size");
    inode->size = new_size;
    write_inode(inode, inode_num);
  } else {
    DEBUG_LOG("Inode size remains unchanged");
  }
}

int read_from_indirect_block(struct wfs_inode *inode, size_t indirect_index,
                             char *block_buffer) {
  int N_DIRECT = N_BLOCKS - 1;

  if (inode->blocks[N_DIRECT] == -1) {
    printf("Indirect block not allocated\n");
    return -1;
  }

  read_data_block(block_buffer, inode->blocks[N_DIRECT]);

  int *indirect_blocks = (int *)block_buffer;
  if (indirect_blocks[indirect_index] == -1) {
    DEBUG_LOG("No data block allocated at indirect index %zu\n",
              indirect_index);
    return -1;
  }

  return indirect_blocks[indirect_index];
}
