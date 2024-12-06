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

int wfs_unlink(const char *path) {
  DEBUG_LOG("Entering wfs_unlink: path = %s\n", path);

  char parent_path[PATH_MAX];
  char file_name[MAX_NAME];

  if (split_path(path, parent_path, file_name) < 0) {
    DEBUG_LOG("Failed to split path: %s\n", path);
    return -EINVAL;
  }

  DEBUG_LOG("Parent path: %s, File name: %s\n", parent_path, file_name);

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

  int inode_num = find_dentry_in_inode(parent_inode_num, file_name);
  if (inode_num == -ENOENT) {
    DEBUG_LOG("File not found in parent directory: %s\n", file_name);
    return -ENOENT;
  }

  struct wfs_inode file_inode;
  read_inode(&file_inode, inode_num);

  if (!S_ISREG(file_inode.mode)) {
    DEBUG_LOG("Path is not a regular file: %s\n", path);
    return -EISDIR;
  }

  free_inode(inode_num);

  if (remove_dentry_in_inode(&parent_inode, inode_num) < 0) {
    DEBUG_LOG("Failed to remove file entry for %s\n", path);
    return -EIO;
  }

  write_inode(&parent_inode, parent_inode_num);

  DEBUG_LOG("File successfully removed: %s\n", path);
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

int wfs_read(const char *path, char *buf, size_t size, off_t offset,
             struct fuse_file_info *fi) {
  DEBUG_LOG("Entering wfs_read: path = %s, size = %zu, offset = %lld\n", path,
            size, (long long)offset);

  int N_DIRECT = N_BLOCKS - 1;
  size_t bytes_read = 0;
  size_t block_offset, to_read;
  char block_buffer[BLOCK_SIZE];

  int inode_num = get_inode_index(path);
  if (inode_num == -ENOENT) {
    DEBUG_LOG("File not found: %s\n", path);
    return -ENOENT;
  }

  struct wfs_inode inode;
  read_inode(&inode, inode_num);

  if (!S_ISREG(inode.mode)) {
    DEBUG_LOG("Path is not a regular file: %s\n", path);
    return -EISDIR;
  }

  DEBUG_LOG("Inode info: size = %zu, blocks = %ld\n", inode.size,
            inode.blocks[0]);

  if (offset >= inode.size) {
    DEBUG_LOG("Offset is beyond the file size: %s\n", path);
    return 0; // No data to read beyond file size
  }

  while (bytes_read < size && offset + bytes_read < inode.size) {
    size_t block_index = (offset + bytes_read) / BLOCK_SIZE;
    block_offset = (offset + bytes_read) % BLOCK_SIZE;

    DEBUG_LOG("block_index = %ld, block_offset = %ld\n", block_index,
              block_offset);

    int data_block_num;

    if (block_index < N_DIRECT) {
      data_block_num = inode.blocks[block_index];
    } else {
      size_t indirect_index = block_index - N_DIRECT;
      data_block_num =
          read_from_indirect_block(&inode, indirect_index, block_buffer);
    }

    if (data_block_num == -1) {
      DEBUG_LOG("No data block allocated at index %zu\n", block_index);
      return -EIO;
    }

    DEBUG_LOG("Reading data block number: %d\n", data_block_num);
    read_data_block(block_buffer, data_block_num);

    DEBUG_LOG("Block %d contents before read:\n", data_block_num);
    for (int i = 0; i < BLOCK_SIZE; i++) {
      DEBUG_LOG("%02x ", (unsigned char)block_buffer[i]);
    }
    DEBUG_LOG("\n");

    to_read = (inode.size - (offset + bytes_read) < BLOCK_SIZE - block_offset)
                  ? inode.size - (offset + bytes_read)
                  : BLOCK_SIZE - block_offset;

    DEBUG_LOG("to_read: %ld\n", to_read);

    memcpy(buf + bytes_read, block_buffer + block_offset, to_read);

    // Debug: Print buffer after copying
    DEBUG_LOG("Buffer after reading data:\n");
    for (size_t i = 0; i < to_read; i++) {
      DEBUG_LOG("%02x ", (unsigned char)buf[bytes_read + i]);
    }
    DEBUG_LOG("\n");

    bytes_read += to_read;
  }

  DEBUG_LOG("Read complete: %zu bytes read from %s\n", bytes_read, path);
  return bytes_read;
}

int wfs_write(const char *path, const char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi) {
  DEBUG_LOG("Entering wfs_write: path = %s, size = %zu, offset = %lld\n", path,
            size, (long long)offset);

  int N_DIRECT = N_BLOCKS - 1;
  size_t bytes_written = 0;
  size_t block_offset, to_write;
  char block_buffer[BLOCK_SIZE];

  int inode_num = get_inode_index(path);
  if (inode_num == -ENOENT) {
    DEBUG_LOG("File not found: %s\n", path);
    return -ENOENT;
  }

  struct wfs_inode inode;
  read_inode(&inode, inode_num);

  if (!S_ISREG(inode.mode)) {
    DEBUG_LOG("Path is not a regular file: %s\n", path);
    return -EISDIR;
  }

  DEBUG_LOG("Inode info: size = %zu, blocks = %ld\n", inode.size,
            inode.blocks[0]);

  while (bytes_written < size) {
    size_t block_index = (offset + bytes_written) / BLOCK_SIZE;
    block_offset = (offset + bytes_written) % BLOCK_SIZE;

    DEBUG_LOG("block_index = %ld, block_offset = %ld\n", block_index,
              block_offset);

    int data_block_num;
    if (block_index < N_DIRECT) {
      data_block_num = allocate_direct_block(&inode, block_index);
    } else {
      data_block_num =
          allocate_indirect_block(&inode, block_index, block_buffer);
      if (data_block_num == -1) {
        data_block_num = allocate_free_data_block();
        if (data_block_num < 0) {
          DEBUG_LOG("Failed to allocate data block for indirect index %zu\n",
                    block_index - N_DIRECT);
          return -EIO;
        }

        int *indirect_blocks = (int *)block_buffer;
        size_t indirect_index = block_index - N_DIRECT;
        indirect_blocks[indirect_index] = data_block_num;
        write_data_block(block_buffer, inode.blocks[N_DIRECT]);
      }
    }

    read_data_block(block_buffer, data_block_num);

    to_write = (size - bytes_written < BLOCK_SIZE - block_offset)
                   ? size - bytes_written
                   : BLOCK_SIZE - block_offset;

    DEBUG_LOG("to_write: %ld\n", to_write);

    memcpy(block_buffer + block_offset, buf + bytes_written, to_write);
    write_data_block(block_buffer, data_block_num);
    DEBUG_LOG("Data written to block number: %d\n", data_block_num);

    bytes_written += to_write;
  }

  if (offset + bytes_written > inode.size) {
    inode.size = offset + bytes_written;
    write_inode(&inode, inode_num);
  }

  DEBUG_LOG("Write complete: %zu bytes written to %s\n", bytes_written, path);
  return bytes_written;
}

int wfs_mknod(const char *path, mode_t mode, dev_t dev) {
  DEBUG_LOG("Entering wfs_mknod: path = %s\n", path);

  char parent_path[PATH_MAX];
  char filename[MAX_NAME];
  split_path(path, parent_path, filename);
  DEBUG_LOG("Split path: parent = %s, filename = %s\n", parent_path, filename);

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

  if (check_duplicate_dentry(&parent_inode, filename) == 0) {
    DEBUG_LOG("File already exists: %s\n", path);
    return -EEXIST;
  }

  int inode_num = allocate_and_init_inode(mode, S_IFREG);
  if (inode_num < 0) {
    DEBUG_LOG("Failed to allocate inode for file: %s\n", path);
    return inode_num;
  }

  if (add_dentry_to_parent(&parent_inode, parent_inode_num, filename,
                           inode_num) < 0) {
    DEBUG_LOG("Failed to add file entry: %s\n", filename);
    return -EIO;
  }

  DEBUG_LOG("File created successfully: %s\n", path);
  return 0;
}

int wfs_mkdir(const char *path, mode_t mode) {
  DEBUG_LOG("Entering wfs_mkdir: path = %s\n", path);

  char parent_path[PATH_MAX];
  char dirname[MAX_NAME];
  split_path(path, parent_path, dirname);
  DEBUG_LOG("Split path: parent = %s, dirname = %s\n", parent_path, dirname);

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

  if (check_duplicate_dentry(&parent_inode, dirname) == 0) {
    DEBUG_LOG("Directory already exists: %s\n", path);
    return -EEXIST;
  }

  int inode_num = allocate_and_init_inode(mode, S_IFDIR);
  if (inode_num < 0) {
    DEBUG_LOG("Failed to allocate inode for directory: %s\n", path);
    return inode_num;
  }

  if (add_dentry_to_parent(&parent_inode, parent_inode_num, dirname,
                           inode_num) < 0) {
    DEBUG_LOG("Failed to add directory entry: %s\n", dirname);
    return -EIO;
  }

  DEBUG_LOG("Directory created successfully: %s\n", path);
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

  for (int i = 0; i < N_BLOCKS && dir_inode.blocks[i] != -1; i++) {
    int block_index = dir_inode.blocks[i];
    int disk_index;
    block_index = get_raid_disk(block_index, &disk_index);
    if (disk_index < 0) {
      DEBUG_LOG("Error: Unable to get disk index for block %ld\n",
                dir_inode.blocks[i]);
      return -EIO;
    }

    size_t block_offset = DATA_BLOCK_OFFSET(block_index);
    DEBUG_LOG("Reading directory block: %d (offset = %zu)\n", block_index,
              block_offset);

    struct wfs_dentry *dentry =
        (struct wfs_dentry *)((char *)wfs_ctx.disk_mmaps[disk_index] +
                              block_offset);
    for (size_t entry_idx = 0;
         entry_idx < BLOCK_SIZE / sizeof(struct wfs_dentry); entry_idx++) {
      if (dentry[entry_idx].num == -1) {
        DEBUG_LOG("Skipping empty directory entry at index %zu\n", entry_idx);
        continue;
      }

      DEBUG_LOG("Adding entry: name = %s, inode = %d\n", dentry[entry_idx].name,
                dentry[entry_idx].num);
      filler(buf, dentry[entry_idx].name, NULL, 0);
    }
  }

  DEBUG_LOG("Adding special entries '.' and '..'\n");
  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);

  fflush(stdout);

  return 0;
}

int wfs_getattr(const char *path, struct stat *stbuf) {
  DEBUG_LOG("Entering wfs_getattr: path = %s\n", path);

  int inode_num = get_inode_index(path);
  if (inode_num == -ENOENT) {
    DEBUG_LOG("File not found: %s\n", path);
    return -ENOENT;
  }

  DEBUG_LOG("Found inode for %s: %d\n", path, inode_num);

  struct wfs_inode inode;

  read_inode(&inode, inode_num);

  DEBUG_LOG("Inode read: mode = %o, size = %ld, nlinks = %d\n", inode.mode,
            inode.size, inode.nlinks);

  memset(stbuf, 0, sizeof(struct stat));
  stbuf->st_mode = inode.mode;
  stbuf->st_nlink = inode.nlinks;
  stbuf->st_size = inode.size;
  stbuf->st_atime = inode.atim;
  stbuf->st_mtime = inode.mtim;
  stbuf->st_ctime = inode.ctim;

  DEBUG_LOG("Attributes populated for %s\n", path);

  fflush(stdout);
  return 0;
}

struct fuse_operations ops = {
    .getattr = wfs_getattr,
    .readdir = wfs_readdir,
    .mkdir = wfs_mkdir,
    .mknod = wfs_mknod,
    .write = wfs_write,
    .read = wfs_read,
    .rmdir = wfs_rmdir,
    .unlink = wfs_unlink,
};
