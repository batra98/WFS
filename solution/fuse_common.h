#ifndef FS_COMMON_H
#define FS_COMMON_H

int read_and_validate_parent_inode(struct wfs_inode *parent_inode,
                                   int parent_inode_num);
int add_file_to_parent(struct wfs_inode *parent_inode, int parent_inode_num,
                       const char *filename, int inode_num);
void populate_stat_from_inode(const struct wfs_inode *inode,
                              struct stat *stbuf);
int load_inode(int inode_num, struct wfs_inode *inode);
int find_inode_for_path(const char *path);
#endif
