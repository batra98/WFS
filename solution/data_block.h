#ifndef DATA_BLOCK_H
#define DATA_BLOCK_H

#include "wfs.h"
#include <stddef.h>
void read_data_block(void *block, size_t block_index);
void write_data_block(const void *block, size_t block_index);
void read_data_block_bitmap(char *data_block_bitmap);
void write_data_block_bitmap(const char *data_block_bitmap);
int allocate_free_data_block();
void free_data_block(int block_index);
void add_dentry_to_dir(struct wfs_inode *parent_inode,
                       struct wfs_dentry *dentry);

#endif
