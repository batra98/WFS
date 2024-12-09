#define FUSE_USE_VERSION 30

#include "data_block.h"
#include "fs_utils.h"
#include "globals.h"
#include "inode.h"
#include "wfs.h"
#include <errno.h>
#include <fuse.h>
#include <linux/limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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
    return 0;
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
