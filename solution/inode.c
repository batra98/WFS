#include "data_block.h"
#include "globals.h"
#include "raid.h"
#include "wfs.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void replicate_if_needed(const void *data, off_t offset, size_t size,
                                int disk_index) {
  if (sb.raid_mode == RAID_1) {
    DEBUG_LOG("Replicating data at offset %ld", offset);
    replicate(data, offset, size, disk_index);
  }
}

void read_inode(struct wfs_inode *inode, size_t inode_index) {
  off_t offset = INODE_OFFSET(inode_index);
  int disk_index = get_raid_disk(offset / BLOCK_SIZE);
  void *inode_offset = (char *)wfs_ctx.disk_mmaps[disk_index] + offset;

  memcpy(inode, inode_offset, sizeof(struct wfs_inode));
  DEBUG_LOG("Read inode at index %zu from disk %d", inode_index, disk_index);
}

void write_inode(const struct wfs_inode *inode, size_t inode_index) {
  off_t offset = INODE_OFFSET(inode_index);
  int disk_index = get_raid_disk(offset / BLOCK_SIZE);
  void *inode_offset = (char *)wfs_ctx.disk_mmaps[disk_index] + offset;

  memcpy(inode_offset, inode, sizeof(struct wfs_inode));
  DEBUG_LOG("Wrote inode at index %zu to disk %d", inode_index, disk_index);

  replicate_if_needed(inode, offset, sizeof(struct wfs_inode), disk_index);
}

void read_inode_bitmap(char *inode_bitmap) {
  size_t inode_bitmap_size = (sb.num_inodes + 7) / 8;
  int disk_index = get_raid_disk(INODE_BITMAP_OFFSET / BLOCK_SIZE);

  memcpy(inode_bitmap,
         (char *)wfs_ctx.disk_mmaps[disk_index] + INODE_BITMAP_OFFSET,
         inode_bitmap_size);
  DEBUG_LOG("Read inode bitmap from disk %d", disk_index);
}

void write_inode_bitmap(const char *inode_bitmap) {
  size_t inode_bitmap_size = (sb.num_inodes + 7) / 8;
  int disk_index = get_raid_disk(INODE_BITMAP_OFFSET / BLOCK_SIZE);

  memcpy((char *)wfs_ctx.disk_mmaps[disk_index] + INODE_BITMAP_OFFSET,
         inode_bitmap, inode_bitmap_size);
  DEBUG_LOG("Wrote inode bitmap to disk %d", disk_index);

  replicate_if_needed(inode_bitmap, INODE_BITMAP_OFFSET, inode_bitmap_size,
                      disk_index);
}

void clear_inode_bitmap(int inode_num) {
  printf("Clearing inode bitmap for inode number: %d\n", inode_num);

  char bitmap_block[BLOCK_SIZE];
  read_inode_bitmap(bitmap_block);

  PRINT_BITMAP("Bitmap before clearing:\n", bitmap_block);

  CLEAR_BIT(bitmap_block, inode_num);

  PRINT_BITMAP("Bitmap after clearing:\n", bitmap_block);

  write_inode_bitmap(bitmap_block);

  printf("Inode bitmap cleared for inode number: %d\n", inode_num);
}

int free_inode(int inode_num) {
  struct wfs_inode inode;
  read_inode(&inode, inode_num);

  free_direct_data_blocks(&inode);
  free_indirect_data_block(&inode);
  clear_inode_bitmap(inode_num);

  DEBUG_LOG("Inode %d successfully freed\n", inode_num);
  return 0;
}

int allocate_free_inode() {
  size_t inode_bitmap_size = (sb.num_inodes + 7) / 8;
  char inode_bitmap[inode_bitmap_size];

  read_inode_bitmap(inode_bitmap);

  for (int i = 0; i < sb.num_inodes; i++) {
    if (!IS_BIT_SET(inode_bitmap, i)) {
      SET_BIT(inode_bitmap, i);
      write_inode_bitmap(inode_bitmap);
      DEBUG_LOG("Allocated inode %d", i);
      return i;
    }
  }

  DEBUG_LOG("No free inodes available");
  return -ENOSPC;
}

int allocate_and_init_inode(mode_t mode, mode_t type_flag) {
  int inode_num = allocate_free_inode();
  if (inode_num < 0) {
    return inode_num;
  }

  struct wfs_inode new_inode = {
      .num = inode_num,
      .mode = mode | type_flag,
      .nlinks = (type_flag == S_IFDIR) ? 2 : 1,
      .size = 0,
      .uid = getuid(),
      .gid = getgid(),
      .atim = time(NULL),
      .mtim = time(NULL),
      .ctim = time(NULL),
  };

  for (int i = 0; i < N_BLOCKS; i++) {
    new_inode.blocks[i] = -1;
  }

  write_inode(&new_inode, inode_num);
  DEBUG_LOG("Initialized inode %d with mode %o", inode_num, new_inode.mode);
  return inode_num;
}

int remove_dentry_in_inode(struct wfs_inode *parent_inode,
                           int target_inode_num) {
  char block_buffer[BLOCK_SIZE];
  for (int i = 0; i < N_BLOCKS; i++) {
    if (parent_inode->blocks[i] == -1)
      continue;

    read_data_block(block_buffer, parent_inode->blocks[i]);
    struct wfs_dentry *entries = (struct wfs_dentry *)block_buffer;

    for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
      if (entries[j].num == target_inode_num) {
        entries[j].num = -1;
        memset(entries[j].name, 0, sizeof(entries[j].name));

        write_data_block(block_buffer, parent_inode->blocks[i]);
        return 0;
      }
    }
  }

  return -1;
}

int is_directory_empty(struct wfs_inode *inode) {
  char block_buffer[BLOCK_SIZE];
  for (int i = 0; i < N_BLOCKS; i++) {
    if (inode->blocks[i] == -1)
      continue;

    read_data_block(block_buffer, inode->blocks[i]);
    struct wfs_dentry *entries = (struct wfs_dentry *)block_buffer;

    for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
      if (entries[j].num != -1 && strcmp(entries[j].name, ".") != 0 &&
          strcmp(entries[j].name, "..") != 0) {
        return 0; // Not empty
      }
    }
  }
  return 1; // Empty
}

int find_dentry_in_inode(int parent_inode_num, const char *name) {
  DEBUG_LOG("Finding dentry in inode %d with name %s", parent_inode_num, name);

  struct wfs_inode parent_inode;
  read_inode(&parent_inode, parent_inode_num);

  for (int i = 0; i < N_BLOCKS; i++) {
    if (parent_inode.blocks[i] == -1) {
      continue;
    }

    size_t num_entries = BLOCK_SIZE / sizeof(struct wfs_dentry);
    for (size_t j = 0; j < num_entries; j++) {
      struct wfs_dentry entry;
      off_t offset = DENTRY_OFFSET(parent_inode.blocks[i], j);
      int disk_index = get_raid_disk(offset);

      memcpy(&entry, (char *)wfs_ctx.disk_mmaps[disk_index] + offset,
             sizeof(struct wfs_dentry));
      if (entry.num == -1) {
        continue;
      }

      if (strcmp(entry.name, name) == 0) {
        DEBUG_LOG("Found dentry: name = %s, num = %d", entry.name, entry.num);
        return entry.num;
      }
    }
  }

  DEBUG_LOG("Dentry not found: name = %s", name);
  return -ENOENT;
}

int get_inode_index(const char *path) {
  if (strcmp(path, "/") == 0) {
    return 0;
  }

  char *path_copy = strdup(path);
  char *component = strtok(path_copy, "/");
  int parent_inode_num = 0;
  int result = 0;

  while (component != NULL) {
    result = find_dentry_in_inode(parent_inode_num, component);
    if (result < 0) {
      free(path_copy);
      DEBUG_LOG("Failed to resolve component %s in path %s", component, path);
      return result;
    }

    parent_inode_num = result;
    component = strtok(NULL, "/");
  }

  free(path_copy);
  DEBUG_LOG("Resolved path %s to inode %d", path, parent_inode_num);
  return parent_inode_num;
}
