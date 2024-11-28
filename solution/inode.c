#include "wfs.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void read_inode(int fd, struct wfs_inode *inode, size_t inode_index,
                struct wfs_sb *sb) {
  off_t inode_offset = sb->i_blocks_ptr + inode_index * BLOCK_SIZE;

  lseek(fd, inode_offset, SEEK_SET);
  read(fd, inode, sizeof(struct wfs_inode));
}

void write_inode(int fd, struct wfs_inode *inode, size_t inode_index,
                 struct wfs_sb *sb) {
  off_t inode_offset = sb->i_blocks_ptr + inode_index * BLOCK_SIZE;

  lseek(fd, inode_offset, SEEK_SET);
  write(fd, inode, sizeof(struct wfs_inode));
}

void read_inode_bitmap(int fd, char *inode_bitmap, struct wfs_sb *sb) {
  size_t inode_bitmap_size = (sb->num_inodes + 7) / 8;
  off_t bitmap_offset = sb->i_blocks_ptr;

  lseek(fd, bitmap_offset, SEEK_SET);
  read(fd, inode_bitmap, inode_bitmap_size);
}

void write_inode_bitmap(int fd, const char *inode_bitmap, struct wfs_sb *sb) {
  size_t inode_bitmap_size = (sb->num_inodes + 7) / 8;
  off_t bitmap_offset = sb->i_bitmap_ptr;

  lseek(fd, bitmap_offset, SEEK_SET);
  write(fd, inode_bitmap, inode_bitmap_size);
}

int allocate_free_inode(int fd, struct wfs_sb *sb) {
  size_t inode_bitmap_size = (sb->num_inodes + 7) / 8;
  char inode_bitmap[inode_bitmap_size];

  read_inode_bitmap(fd, inode_bitmap, sb);

  for (int i = 0; i < sb->num_inodes; i++) {
    if (!(inode_bitmap[i / 8] & (1 << (i % 8)))) {
      inode_bitmap[i / 8] |= (1 << (i % 8));
      write_inode_bitmap(fd, inode_bitmap, sb);
      return i;
    }
  }

  return -ENOSPC;
}

static int find_dentry_in_inode(int parent_inode_num, const char *name) {
  struct wfs_inode parent_inode;
  read_inode(disk_fd, &parent_inode, parent_inode_num, &sb);

  for (int i = 0; i < N_BLOCKS; i++) {
    if (parent_inode.blocks[i] == 0) {
      continue;
    }

    size_t num_entries = BLOCK_SIZE / sizeof(struct wfs_dentry);
    for (size_t j = 0; j < num_entries; j++) {
      struct wfs_dentry entry;
      off_t offset = parent_inode.blocks[i] + j * sizeof(struct wfs_dentry);
      lseek(disk_fd, offset, SEEK_SET);
      read(disk_fd, &entry, sizeof(struct wfs_dentry));

      if (strcmp(entry.name, name) == 0) {
        return entry.num;
      }
    }
  }

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
      return result;
    }

    parent_inode_num = result;
    component = strtok(NULL, "/");
  }

  free(path_copy);
  return parent_inode_num;
}
