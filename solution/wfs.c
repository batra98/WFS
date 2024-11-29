#define FUSE_USE_VERSION 30

#include "wfs.h"
#include "data_block.h"
#include "fs_utils.h"
#include "fuse.h"
#include "globals.h"
#include "inode.h"
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
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

int open_disk(const char *path) {
  printf("Opening disk: %s\n", path);
  int disk_fd = open(path, O_RDWR);
  if (disk_fd == -1) {
    perror("Failed to open disk");
    return -1;
  }

  printf("Reading superblock...\n");
  if (read(disk_fd, &sb, sizeof(struct wfs_sb)) != sizeof(struct wfs_sb)) {
    perror("Failed to read superblock");
    close(disk_fd);
    return -1;
  }

  int disk_size = calculate_required_size(sb.num_inodes, sb.num_data_blocks);

  // Map the disk file into memory
  disk_mmap =
      mmap(NULL, disk_size, PROT_READ | PROT_WRITE, MAP_SHARED, disk_fd, 0);
  if (disk_mmap == MAP_FAILED) {
    perror("Failed to mmap disk");
    close(disk_fd);
    return -1;
  }

  close(disk_fd);

  printf("Superblock read successfully!\n");
  print_superblock();

  return 0;
}

/*int wfs_mkdir(const char *path, mode_t mode) {
  char parent_path[PATH_MAX];
  char dirname[MAX_NAME];
  split_path(path, parent_path, dirname);

  int parent_inode_num = get_inode_index(parent_path);
  if (parent_inode_num == -ENOENT) {
    return -ENOENT;
  }

  struct wfs_inode parent_inode;
  read_inode(disk_fd, &parent_inode, parent_inode_num, &sb);

  if (!S_ISDIR(parent_inode.mode)) {
    return -ENOTDIR;
  }

  struct wfs_dentry dentry;
  int dir_exists = 0;
  for (int i = 0; i < N_BLOCKS && parent_inode.blocks[i] != 0; i++) {
    off_t block_offset = parent_inode.blocks[i] * BLOCK_SIZE;
    for (off_t entry_offset = 0; entry_offset < BLOCK_SIZE;
         entry_offset += sizeof(struct wfs_dentry)) {
      if (pread(disk_fd, &dentry, sizeof(struct wfs_dentry),
                block_offset + entry_offset) != sizeof(struct wfs_dentry)) {
        perror("Failed to read directory entry");
        return -EIO;
      }
      if (dentry.num != -1 && strcmp(dentry.name, dirname) == 0) {
        dir_exists = 1;
        break;
      }
    }
  }

  if (dir_exists) {
    return -EEXIST;
  }

  int inode_num = allocate_free_inode(disk_fd, &sb);
  if (inode_num == -ENOSPC) {
    return -ENOSPC;
  }

  struct wfs_inode new_inode = {
      .mode = mode | S_IFDIR,
      .nlinks = 2,
      .size = 2 * sizeof(struct wfs_dentry),
      .atim = time(NULL),
      .mtim = time(NULL),
      .ctim = time(NULL),
  };

  write_inode(disk_fd, &new_inode, inode_num, &sb);

  struct wfs_dentry new_dentry = {
      .num = inode_num,
      .name = {0},
  };

  strncpy(new_dentry.name, ".", MAX_NAME);
  add_dentry_to_dir(&parent_inode, &new_dentry);

  struct wfs_inode dir_inode;
  read_inode(disk_fd, &dir_inode, inode_num, &sb);
  struct wfs_dentry dotdot_dentry = {
      .num = parent_inode_num,
      .name = {0},
  };
  strncpy(dotdot_dentry.name, "..", MAX_NAME);
  add_dentry_to_dir(&dir_inode, &dotdot_dentry);

  return 0;
}*/

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
  read_inode(&dir_inode, inode_num);

  if (!S_ISDIR(dir_inode.mode)) {
    printf("Path is not a directory: %s\n", path);
    return -ENOTDIR;
  }

  printf("Directory inode read successfully: mode = %o, size = %ld\n",
         dir_inode.mode, dir_inode.size);

  for (int i = 0; i < N_BLOCKS && dir_inode.blocks[i] != 0; i++) {
    size_t block_offset = sb.d_blocks_ptr + (dir_inode.blocks[i] * BLOCK_SIZE);
    printf("Reading directory block: %ld (offset = %zu)\n", dir_inode.blocks[i],
           block_offset);

    struct wfs_dentry *dentry =
        (struct wfs_dentry *)((char *)disk_mmap + block_offset);
    for (size_t entry_idx = 0;
         entry_idx < BLOCK_SIZE / sizeof(struct wfs_dentry);
         entry_idx++, dentry++) {
      if (dentry->num == -1) {
        printf("Skipping empty directory entry at index %zu\n", entry_idx);
        continue;
      }

      printf("Adding entry: name = %s, inode = %d\n", dentry->name,
             dentry->num);
      filler(buf, dentry->name, NULL, 0);
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

  read_inode(&inode, inode_num);

  printf("Inode read: mode = %o, size = %ld, nlinks = %d\n", inode.mode,
         inode.size, inode.nlinks);

  memset(stbuf, 0, sizeof(struct stat));
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
