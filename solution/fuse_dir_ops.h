#ifndef FS_DIR_OPS_H
#define FS_DIR_OPS_H

#define FUSE_USE_VERSION 30
#include <fuse.h>
#include <stddef.h>
#include <sys/stat.h>

int wfs_mkdir(const char *path, mode_t mode);
int wfs_rmdir(const char *path);
int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                off_t offset, struct fuse_file_info *fi);
#endif
