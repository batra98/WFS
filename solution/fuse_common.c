#include "data_block.h"
#include "globals.h"
#include "inode.h"
#include <errno.h>
#include <string.h>

int read_and_validate_parent_inode(struct wfs_inode *parent_inode,
                                   int parent_inode_num) {
  read_inode(parent_inode, parent_inode_num);

  if (!S_ISDIR(parent_inode->mode)) {
    DEBUG_LOG("Parent is not a directory: inode_num = %d", parent_inode_num);
    return -ENOTDIR;
  }
  DEBUG_LOG("Parent directory validated: inode_num = %d", parent_inode_num);
  return 0;
}

int add_file_to_parent(struct wfs_inode *parent_inode, int parent_inode_num,
                       const char *filename, int inode_num) {
  if (add_dentry_to_parent(parent_inode, parent_inode_num, filename,
                           inode_num) < 0) {
    DEBUG_LOG("Failed to add directory entry for file: %s", filename);
    return -EIO;
  }
  DEBUG_LOG("Added file to parent directory: %s", filename);
  return 0;
}

int find_inode_for_path(const char *path) {
  int inode_num = get_inode_index(path);

  if (inode_num == -ENOENT) {
    DEBUG_LOG("File not found: %s", path);
    return -ENOENT;
  }

  DEBUG_LOG("Found inode for %s: %d", path, inode_num);
  return inode_num;
}

int load_inode(int inode_num, struct wfs_inode *inode) {
  if (!inode) {
    DEBUG_LOG("Invalid inode pointer for inode number: %d", inode_num);
    return -EIO;
  }

  read_inode(inode, inode_num);
  DEBUG_LOG("Inode read: mode = %o, size = %ld, nlinks = %d", inode->mode,
            inode->size, inode->nlinks);

  return 0;
}

void populate_stat_from_inode(const struct wfs_inode *inode,
                              struct stat *stbuf) {
  if (!inode || !stbuf) {
    DEBUG_LOG("Invalid arguments to populate_stat_from_inode");
    return;
  }

  memset(stbuf, 0, sizeof(struct stat));
  stbuf->st_mode = inode->mode;
  stbuf->st_nlink = inode->nlinks;
  stbuf->st_size = inode->size;
  stbuf->st_atime = inode->atim;
  stbuf->st_mtime = inode->mtim;
  stbuf->st_ctime = inode->ctim;

  DEBUG_LOG("Stat structure populated: mode = %o, size = %ld, nlinks = %ld",
            stbuf->st_mode, stbuf->st_size, stbuf->st_nlink);
}
