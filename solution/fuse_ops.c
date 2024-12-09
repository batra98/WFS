#define FUSE_USE_VERSION 30

#include "fuse_dir_ops.h"
#include "fuse_file_ops.h"
#include "fuse_meta_ops.h"

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
