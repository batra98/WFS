
#ifndef INODE_H
#define INODE_H

#include "wfs.h"
#include <stddef.h>

void write_inode(int fd, struct wfs_inode *inode, size_t inode_index,
                 struct wfs_sb *sb);

void read_inode(int fd, struct wfs_inode *inode, size_t inode_index,
                struct wfs_sb *sb);

void read_inode_bitmap(int fd, char *inode_bitmap, struct wfs_sb *sb);

void write_inode_bitmap(int fd, const char *inode_bitmap, struct wfs_sb *sb);

int allocate_free_inode(int fd, struct wfs_sb *sb);

int get_inode_index(const char *path);

#endif
