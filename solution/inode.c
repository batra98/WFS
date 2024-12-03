#include "globals.h"
#include "raid.h"
#include "wfs.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void *get_disk_mmap(size_t offset) {
  int disk_index = get_raid_disk(offset / BLOCK_SIZE);
  if (disk_index < 0) {
    return NULL;
  }
  return (char *)wfs_ctx.disk_mmaps[disk_index] + offset;
}

void read_inode(struct wfs_inode *inode, size_t inode_index) {
  off_t inode_offset = INODE_OFFSET(inode_index);
  void *disk_ptr = get_disk_mmap(inode_offset);
  if (disk_ptr == NULL) {
    perror("Error accessing disk for inode read");
    return;
  }
  memcpy(inode, disk_ptr, sizeof(struct wfs_inode));
}

void write_inode(const struct wfs_inode *inode, size_t inode_index) {
  off_t inode_offset = INODE_OFFSET(inode_index);
  void *disk_ptr = get_disk_mmap(inode_offset);
  if (disk_ptr == NULL) {
    perror("Error accessing disk for inode write");
    return;
  }
  memcpy(disk_ptr, inode, sizeof(struct wfs_inode));
}

void read_inode_bitmap(char *inode_bitmap) {
  size_t inode_bitmap_size = (sb.num_inodes + 7) / 8;
  void *disk_ptr = get_disk_mmap(INODE_BITMAP_OFFSET);
  if (disk_ptr == NULL) {
    perror("Error accessing disk for inode bitmap read");
    return;
  }
  memcpy(inode_bitmap, disk_ptr, inode_bitmap_size);
}

void write_inode_bitmap(const char *inode_bitmap) {
  size_t inode_bitmap_size = (sb.num_inodes + 7) / 8;
  void *disk_ptr = get_disk_mmap(INODE_BITMAP_OFFSET);
  if (disk_ptr == NULL) {
    perror("Error accessing disk for inode bitmap write");
    return;
  }
  memcpy(disk_ptr, inode_bitmap, inode_bitmap_size);
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
  printf("Entering find_dentry_in_inode: parent_inode_num = %d, name = %s\n",
         parent_inode_num, name);

  struct wfs_inode parent_inode;
  read_inode(&parent_inode, parent_inode_num);
  printf("Read parent inode: mode = %o, nlinks = %d, size = %ld\n",
         parent_inode.mode, parent_inode.nlinks, parent_inode.size);

  for (int i = 0; i < N_BLOCKS; i++) {
    if (parent_inode.blocks[i] == 0) {
      continue;
    }

    size_t num_entries = BLOCK_SIZE / sizeof(struct wfs_dentry);
    printf("Checking block %d with %zu entries\n", i, num_entries);

    for (size_t j = 0; j < num_entries; j++) {
      struct wfs_dentry entry;
      off_t offset = DENTRY_OFFSET(parent_inode.blocks[i], j);
      printf("Reading entry %zu at offset %ld\n", j, offset);
      void *disk_ptr = get_disk_mmap(offset);
      if (disk_ptr == NULL) {
        perror("Error accessing disk for dentry read");
        continue;
      }
      memcpy(&entry, disk_ptr, sizeof(struct wfs_dentry));

      printf("Entry %zu: name = %s, num = %d\n", j, entry.name, entry.num);

      if (strcmp(entry.name, name) == 0) {
        printf("Found directory entry: name = %s, num = %d\n", entry.name,
               entry.num);
        return entry.num;
      }
    }
  }

  printf("Directory entry not found: name = %s\n", name);
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
