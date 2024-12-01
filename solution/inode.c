#include "globals.h"
#include "wfs.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define INODE_OFFSET(index) (sb.i_blocks_ptr + (index) * BLOCK_SIZE)
#define INODE_BITMAP_OFFSET sb.i_bitmap_ptr
#define DENTRY_OFFSET(block, index)                                            \
  ((block) * BLOCK_SIZE + (index) * sizeof(struct wfs_dentry))

void read_inode(struct wfs_inode *inode, size_t inode_index) {
  off_t inode_offset = INODE_OFFSET(inode_index);
  memcpy(inode, (char *)disk_mmap + inode_offset, sizeof(struct wfs_inode));
}

void write_inode(const struct wfs_inode *inode, size_t inode_index) {
  off_t inode_offset = INODE_OFFSET(inode_index);
  memcpy((char *)disk_mmap + inode_offset, inode, sizeof(struct wfs_inode));
}

void read_inode_bitmap(char *inode_bitmap) {
  size_t inode_bitmap_size = (sb.num_inodes + 7) / 8;
  memcpy(inode_bitmap, (char *)disk_mmap + INODE_BITMAP_OFFSET,
         inode_bitmap_size);
}

void write_inode_bitmap(const char *inode_bitmap) {
  size_t inode_bitmap_size = (sb.num_inodes + 7) / 8;
  memcpy((char *)disk_mmap + INODE_BITMAP_OFFSET, inode_bitmap,
         inode_bitmap_size);
}

int allocate_free_inode() {
  size_t inode_bitmap_size = (sb.num_inodes + 7) / 8;
  char inode_bitmap[inode_bitmap_size];

  read_inode_bitmap(inode_bitmap);

  for (int i = 0; i < sb.num_inodes; i++) {
    if (!(inode_bitmap[i / 8] & (1 << (i % 8)))) {
      inode_bitmap[i / 8] |= (1 << (i % 8));
      write_inode_bitmap(inode_bitmap);
      return i;
    }
  }

  return -ENOSPC;
}

int find_dentry_in_inode(int parent_inode_num, const char *name) {
  struct wfs_inode parent_inode;
  read_inode(&parent_inode, parent_inode_num);

  for (int i = 0; i < N_BLOCKS; i++) {
    if (parent_inode.blocks[i] == 0) {
      continue;
    }

    size_t num_entries = BLOCK_SIZE / sizeof(struct wfs_dentry);
    for (size_t j = 0; j < num_entries; j++) {
      struct wfs_dentry entry;
      off_t offset = DENTRY_OFFSET(parent_inode.blocks[i], j);
      memcpy(&entry, (char *)disk_mmap + offset, sizeof(struct wfs_dentry));

      if (strcmp(entry.name, name) == 0) {
        return entry.num;
      }
    }
  }

  return -ENOENT;
}

int get_inode_index(const char *path) {
  if (strcmp(path, "/") == NULL) {
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
      return result;
    }

    parent_inode_num = result;
    component = strtok(NULL, "/");
  }

  free(path_copy);
  return parent_inode_num;
}
