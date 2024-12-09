#include "raid.h"
#define FUSE_USE_VERSION 30

#include "data_block.h"
#include "fs_utils.h"
#include "fuse_common.h"
#include "globals.h"
#include "inode.h"
#include "wfs.h"
#include <errno.h>
#include <fuse.h>
#include <linux/limits.h>
#include <unistd.h>

int wfs_mkdir(const char *path, mode_t mode) {
  DEBUG_LOG("Entering wfs_mkdir: path = %s", path);

  char parent_path[PATH_MAX];
  char dirname[MAX_NAME];
  split_path(path, parent_path, dirname);
  DEBUG_LOG("Path split into: parent = %s, dirname = %s", parent_path, dirname);

  int parent_inode_num = get_inode_index(parent_path);
  if (parent_inode_num == -ENOENT) {
    ERROR_LOG("Parent directory not found: %s", parent_path);
    return -ENOENT;
  }

  struct wfs_inode parent_inode;
  if (read_and_validate_parent_inode(&parent_inode, parent_inode_num) != 0) {
    ERROR_LOG("Parent is not a valid directory: %s", parent_path);
    return -ENOTDIR;
  }

  if (check_duplicate_dentry(&parent_inode, dirname) == 0) {
    DEBUG_LOG("Directory already exists: %s", path);
    return -EEXIST;
  }

  int inode_num = allocate_and_init_inode(mode, S_IFDIR);
  if (inode_num < 0) {
    ERROR_LOG("Failed to allocate inode for directory: %s", path);
    return inode_num;
  }

  if (add_dentry_to_parent(&parent_inode, parent_inode_num, dirname,
                           inode_num) < 0) {
    ERROR_LOG("Failed to add directory entry: %s to parent: %s", dirname,
              parent_path);
    return -EIO;
  }

  DEBUG_LOG("Directory created successfully: %s", path);
  return 0;
}

int wfs_rmdir(const char *path) {
  DEBUG_LOG("Entering wfs_rmdir: path = %s\n", path);

  char parent_path[PATH_MAX];
  char dir_name[MAX_NAME];

  if (split_path(path, parent_path, dir_name) < 0) {
    DEBUG_LOG("Failed to split path: %s\n", path);
    return -EINVAL;
  }

  DEBUG_LOG("Parent path: %s, Directory name: %s\n", parent_path, dir_name);

  int parent_inode_num = get_inode_index(parent_path);
  if (parent_inode_num == -ENOENT) {
    DEBUG_LOG("Parent directory not found: %s\n", parent_path);
    return -ENOENT;
  }

  struct wfs_inode parent_inode;
  read_inode(&parent_inode, parent_inode_num);

  if (!S_ISDIR(parent_inode.mode)) {
    DEBUG_LOG("Parent is not a directory: %s\n", parent_path);
    return -ENOTDIR;
  }

  int inode_num = find_dentry_in_inode(parent_inode_num, dir_name);
  if (inode_num == -ENOENT) {
    DEBUG_LOG("Child not found in parent directory: %s\n", dir_name);
    return -ENOENT;
  }

  struct wfs_inode inode;
  read_inode(&inode, inode_num);

  if (!S_ISDIR(inode.mode)) {
    DEBUG_LOG("Path is not a directory: %s\n", path);
    return -ENOTDIR;
  }

  if (!is_directory_empty(&inode)) {
    DEBUG_LOG("Directory is not empty: %s\n", path);
    return -ENOTEMPTY;
  }

  free_inode(inode_num);

  if (remove_dentry_in_inode(&parent_inode, inode_num) < 0) {
    DEBUG_LOG("Failed to remove directory entry for %s\n", path);
    return -EIO;
  }

  write_inode(&parent_inode, parent_inode_num);

  DEBUG_LOG("Directory successfully removed: %s\n", path);
  return 0;
}

static int read_and_fill_directory_entries(const struct wfs_inode *dir_inode,
                                           void *buf, fuse_fill_dir_t filler) {
  for (int i = 0; i < N_BLOCKS && dir_inode->blocks[i] != -1; i++) {
    int block_index = dir_inode->blocks[i];
    int disk_index;
    block_index = get_raid_disk(block_index, &disk_index);

    if (disk_index < 0) {
      DEBUG_LOG("Error: Unable to get disk index for block %ld",
                dir_inode->blocks[i]);
      return -EIO;
    }

    size_t block_offset = DATA_BLOCK_OFFSET(block_index);
    DEBUG_LOG("Reading directory block: %d (offset = %zu)", block_index,
              block_offset);

    struct wfs_dentry *dentry =
        (struct wfs_dentry *)((char *)wfs_ctx.disk_mmaps[disk_index] +
                              block_offset);

    for (size_t entry_idx = 0;
         entry_idx < BLOCK_SIZE / sizeof(struct wfs_dentry); entry_idx++) {
      if (dentry[entry_idx].num == -1) {
        DEBUG_LOG("Skipping empty directory entry at index %zu", entry_idx);
        continue;
      }

      DEBUG_LOG("Adding entry: name = %s, inode = %d", dentry[entry_idx].name,
                dentry[entry_idx].num);
      filler(buf, dentry[entry_idx].name, NULL, 0);
    }
  }

  return 0;
}

int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                off_t offset, struct fuse_file_info *fi) {
  DEBUG_LOG("Entering wfs_readdir: path = %s\n", path);
  (void)offset;
  (void)fi;

  int inode_num = get_inode_index(path);
  if (inode_num == -ENOENT) {
    DEBUG_LOG("Directory not found: %s\n", path);
    return -ENOENT;
  }

  DEBUG_LOG("Found inode for %s: %d\n", path, inode_num);

  struct wfs_inode dir_inode;
  read_inode(&dir_inode, inode_num);

  if (!S_ISDIR(dir_inode.mode)) {
    DEBUG_LOG("Path is not a directory: %s\n", path);
    return -ENOTDIR;
  }

  DEBUG_LOG("Directory inode read successfully: mode = %o, size = %ld\n",
            dir_inode.mode, dir_inode.size);

  DEBUG_LOG("Reading directory entries for path: %s", path);
  if (read_and_fill_directory_entries(&dir_inode, buf, filler) != 0) {
    return -EIO;
  }

  DEBUG_LOG("Adding special entries '.' and '..'");
  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);

  DEBUG_LOG("Successfully exited wfs_readdir for path: %s", path);
  return 0;
}
