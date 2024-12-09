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

int wfs_mknod(const char *path, mode_t mode, dev_t dev) {
  DEBUG_LOG("Entering wfs_mknod: path = %s", path);

  char parent_path[PATH_MAX];
  char filename[MAX_NAME];
  split_path(path, parent_path, filename);
  DEBUG_LOG("Path split: parent = %s, filename = %s", parent_path, filename);

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

  if (check_duplicate_dentry(&parent_inode, filename) == 0) {
    DEBUG_LOG("File or directory already exists: %s", path);
    return -EEXIST;
  }

  int inode_num = allocate_and_init_inode(mode, S_IFREG);
  if (inode_num < 0) {
    ERROR_LOG("Failed to allocate inode for file: %s", path);
    return inode_num;
  }

  if (add_file_to_parent(&parent_inode, parent_inode_num, filename,
                         inode_num) != 0) {
    ERROR_LOG("Failed to add file entry: %s to parent: %s", filename,
              parent_path);
    return -EIO;
  }

  DEBUG_LOG("File created successfully: %s", path);
  return 0;
}

int wfs_getattr(const char *path, struct stat *stbuf) {
  DEBUG_LOG("Entering wfs_getattr: path = %s", path);

  int inode_num = find_inode_for_path(path);
  if (inode_num < 0) {
    return inode_num;
  }

  struct wfs_inode inode;
  if (load_inode(inode_num, &inode) != 0) {
    return -EIO;
  }

  populate_stat_from_inode(&inode, stbuf);
  DEBUG_LOG("Attributes populated successfully for %s", path);

  return 0;
}
