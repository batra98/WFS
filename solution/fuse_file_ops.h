#ifndef FS_FILE_OPS_H
#define FS_FILE_OPS_H

#define FUSE_USE_VERSION 30
#include <fuse.h>
#include <stddef.h>
#include <sys/stat.h>

int wfs_write(const char *path, const char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi);
int wfs_read(const char *path, char *buf, size_t size, off_t offset,
             struct fuse_file_info *fi);
int wfs_unlink(const char *path);
#endif
