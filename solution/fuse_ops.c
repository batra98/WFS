#define FUSE_USE_VERSION 30

#include "data_block.h"
#include "fs_utils.h"
#include "globals.h"
#include "inode.h"
#include "raid.h"
#include "wfs.h"
#include <errno.h>
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int wfs_mkdir(const char *path, mode_t mode) {
  printf("Entering wfs_mkdir: path = %s\n", path);

  char parent_path[PATH_MAX];
  char dirname[MAX_NAME];
  split_path(path, parent_path, dirname);
  printf("Split path: parent = %s, dirname = %s\n", parent_path, dirname);

  int parent_inode_num = get_inode_index(parent_path);
  if (parent_inode_num == -ENOENT) {
    printf("Parent directory not found: %s\n", parent_path);
    return -ENOENT;
  }

  struct wfs_inode parent_inode;
  read_inode(&parent_inode, parent_inode_num);

  if (!S_ISDIR(parent_inode.mode)) {
    printf("Parent is not a directory: %s\n", parent_path);
    return -ENOTDIR;
  }

  if (check_duplicate_dentry(&parent_inode, dirname) == 0) {
    printf("Directory already exists: %s\n", path);
    return -EEXIST;
  }

  int inode_num = allocate_and_init_inode(mode, S_IFDIR);
  if (inode_num < 0) {
    printf("Failed to allocate inode for directory: %s\n", path);
    return inode_num;
  }

  if (add_dentry_to_parent(&parent_inode, parent_inode_num, dirname,
                           inode_num) < 0) {
    printf("Failed to add directory entry: %s\n", dirname);
    return -EIO;
  }

  printf("Directory created successfully: %s\n", path);
  return 0;
}

int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                off_t offset, struct fuse_file_info *fi) {
  printf("Entering wfs_readdir: path = %s\n", path);
  (void)offset;
  (void)fi;

  int inode_num = get_inode_index(path);
  if (inode_num == -ENOENT) {
    printf("Directory not found: %s\n", path);
    return -ENOENT;
  }

  printf("Found inode for %s: %d\n", path, inode_num);

  struct wfs_inode dir_inode;
  read_inode(&dir_inode, inode_num);

  if (!S_ISDIR(dir_inode.mode)) {
    printf("Path is not a directory: %s\n", path);
    return -ENOTDIR;
  }

  printf("Directory inode read successfully: mode = %o, size = %ld\n",
         dir_inode.mode, dir_inode.size);

  for (int i = 0; i < N_BLOCKS && dir_inode.blocks[i] != -1; i++) {
    int disk_index = get_raid_disk(dir_inode.blocks[i] / BLOCK_SIZE);
    if (disk_index < 0) {
      printf("Error: Unable to get disk index for block %ld\n",
             dir_inode.blocks[i]);
      return -EIO;
    }

    size_t block_offset = DATA_BLOCK_OFFSET(dir_inode.blocks[i]);
    printf("Reading directory block: %ld (offset = %zu)\n", dir_inode.blocks[i],
           block_offset);

    struct wfs_dentry *dentry =
        (struct wfs_dentry *)((char *)wfs_ctx.disk_mmaps[disk_index] +
                              block_offset);
    for (size_t entry_idx = 0;
         entry_idx < BLOCK_SIZE / sizeof(struct wfs_dentry); entry_idx++) {
      if (dentry[entry_idx].num == -1) {
        printf("Skipping empty directory entry at index %zu\n", entry_idx);
        continue;
      }

      printf("Adding entry: name = %s, inode = %d\n", dentry[entry_idx].name,
             dentry[entry_idx].num);
      filler(buf, dentry[entry_idx].name, NULL, 0);
    }
  }

  printf("Adding special entries '.' and '..'\n");
  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);

  fflush(stdout);

  return 0;
}

int wfs_getattr(const char *path, struct stat *stbuf) {
  printf("Entering wfs_getattr: path = %s\n", path);

  int inode_num = get_inode_index(path);
  if (inode_num == -ENOENT) {
    printf("File not found: %s\n", path);
    return -ENOENT;
  }

  printf("Found inode for %s: %d\n", path, inode_num);

  struct wfs_inode inode;

  read_inode(&inode, inode_num);

  printf("Inode read: mode = %o, size = %ld, nlinks = %d\n", inode.mode,
         inode.size, inode.nlinks);

  memset(stbuf, 0, sizeof(struct stat));
  stbuf->st_mode = inode.mode;
  stbuf->st_nlink = inode.nlinks;
  stbuf->st_size = inode.size;
  stbuf->st_atime = inode.atim;
  stbuf->st_mtime = inode.mtim;
  stbuf->st_ctime = inode.ctim;

  printf("Attributes populated for %s\n", path);

  fflush(stdout);
  return 0;
}

struct fuse_operations ops = {
    .getattr = wfs_getattr,
    .readdir = wfs_readdir,
    .mkdir = wfs_mkdir,
};
