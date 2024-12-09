#ifndef FS_META_OPS_H
#define FS_META_OPS_H

#define FUSE_USE_VERSION 30
#include <fuse.h>
#include <stddef.h>
#include <sys/stat.h>

int wfs_getattr(const char *path, struct stat *stbuf);
int wfs_mknod(const char *path, mode_t mode, dev_t dev);
#endif
