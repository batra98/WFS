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
int check_duplicate_dentry(const struct wfs_inode *parent_inode,
                           const char *dirname);
int add_dentry_to_parent(struct wfs_inode *parent_inode, int parent_inode_num,
                         const char *dirname, int inode_num);
int allocate_indirect_block(struct wfs_inode *inode, size_t block_index,
                            char *block_buffer);
int allocate_direct_block(struct wfs_inode *inode, size_t block_index);
void update_inode_size(struct wfs_inode *inode, size_t inode_num,
                       off_t new_size);
int read_from_indirect_block(struct wfs_inode *inode, size_t indirect_index,
                             char *block_buffer);
void free_direct_data_blocks(struct wfs_inode *inode);
void free_indirect_data_block(struct wfs_inode *inode);
#endif
