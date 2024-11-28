#define FUSE_USE_VERSION 30

#include "wfs.h"
#include "fuse.h"
#include "globals.h"
#include "inode.h"
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void print_superblock() {
  printf("Superblock Contents:\n");
  printf("  Total Blocks: %ld\n", sb.num_data_blocks);
  printf("  Inode Count: %ld\n", sb.num_inodes);
  printf("  Data Blocks Pointer: %ld\n", sb.d_blocks_ptr);
  printf("  Inode Blocks Pointer: %ld\n", sb.i_blocks_ptr);
  printf("  Inode Bitmap Pointer: %ld\n", sb.i_bitmap_ptr);
  printf("  Data Bitmap Pointer: %ld\n", sb.d_bitmap_ptr);
}

void open_disk(const char *path) {
  printf("Opening disk: %s\n", path);
  disk_fd = open(path, O_RDWR);
  if (disk_fd == -1) {
    perror("Failed to open disk");
    return;
  }

  printf("Reading superblock...\n");
  if (read(disk_fd, &sb, sizeof(struct wfs_sb)) != sizeof(struct wfs_sb)) {
    perror("Failed to read superblock");
    close(disk_fd);
    return;
  }

  printf("Superblock read successfully!\n");
  print_superblock();
}

int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                off_t offset, struct fuse_file_info *fi) {
  printf("Entering wfs_readdir: path = %s\n", path);
  (void)offset;
  (void)fi;

  int inode_num = get_inode_index(path);
  if (inode_num == -ENOENT) {
    printf("Directory not found: %s\n", path);
    return -ENOENT;
  }

  printf("Found inode for %s: %d\n", path, inode_num);

  struct wfs_inode dir_inode;
  read_inode(disk_fd, &dir_inode, inode_num, &sb);

  if (!S_ISDIR(dir_inode.mode)) {
    printf("Path is not a directory: %s\n", path);
    return -ENOTDIR;
  }

  printf("Directory inode read successfully: mode = %o, size = %ld\n",
         dir_inode.mode, dir_inode.size);

  struct wfs_dentry dentry;
  for (int i = 0; i < N_BLOCKS && dir_inode.blocks[i] != 0; i++) {
    off_t block_offset = dir_inode.blocks[i] * BLOCK_SIZE;
    printf("Reading directory block: %ld (offset = %ld)\n", dir_inode.blocks[i],
           block_offset);

    for (off_t entry_offset = 0; entry_offset < BLOCK_SIZE;
         entry_offset += sizeof(struct wfs_dentry)) {
      if (pread(disk_fd, &dentry, sizeof(struct wfs_dentry),
                block_offset + entry_offset) != sizeof(struct wfs_dentry)) {
        perror("Failed to read directory entry");
        return -EIO;
      }

      if (dentry.num == -1) {
        printf("Skipping empty directory entry at offset %ld\n", entry_offset);
        continue;
      }

      printf("Adding entry: name = %s, inode = %d\n", dentry.name, dentry.num);
      filler(buf, dentry.name, NULL, 0);
    }
  }

  printf("Adding special entries '.' and '..'\n");
  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);

  fflush(stdout);

  return 0;
}

int wfs_getattr(const char *path, struct stat *stbuf) {
  printf("Entering wfs_getattr: path = %s\n", path);

  int inode_num = get_inode_index(path);
  if (inode_num == -ENOENT) {
    printf("File not found: %s\n", path);
    return -ENOENT;
  }

  printf("Found inode for %s: %d\n", path, inode_num);

  struct wfs_inode inode;
  read_inode(disk_fd, &inode, inode_num, &sb);

  printf("Inode read: mode = %o, size = %ld, nlinks = %d\n", inode.mode,
         inode.size, inode.nlinks);

  stbuf->st_mode = inode.mode;
  stbuf->st_nlink = inode.nlinks;
  stbuf->st_size = inode.size;
  stbuf->st_atime = inode.atim;
  stbuf->st_mtime = inode.mtim;
  stbuf->st_ctime = inode.ctim;

  printf("Attributes populated for %s\n", path);

  fflush(stdout);
  return 0;
}

static struct fuse_operations ops = {
    .getattr = wfs_getattr,
    .readdir = wfs_readdir,
};

void print_arguments(int argc, char *argv[]) {
  printf("Arguments passed to the program:\n");
  for (int i = 0; i < argc; i++) {
    printf("  argv[%d]: %s\n", i, argv[i]);
  }
}

int main(int argc, char *argv[]) {
  if (argc < 4) {
    fprintf(stderr, "Usage: %s disk1 disk2 [FUSE options] mount_point\n",
            argv[0]);
    return 1;
  }

  printf("Starting WFS with disk: %s\n", argv[1]);
  open_disk(argv[1]);

  printf("Initializing FUSE with mount point: %s\n", argv[argc - 1]);
  print_arguments(argc - 2, argv + 2);
  return fuse_main(argc - 2, argv + 2, &ops, NULL);
}
