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

int wfs_mkdir(const char *path, mode_t mode) {
  printf("Entering wfs_mkdir: path = %s\n", path);

  char parent_path[PATH_MAX];
  char dirname[MAX_NAME];
  split_path(path, parent_path, dirname);
  printf("Split path: parent = %s, dirname = %s\n", parent_path, dirname);

  int parent_inode_num = get_inode_index(parent_path);
  printf("Parent inode number: %d\n", parent_inode_num);
  if (parent_inode_num == -ENOENT) {
    printf("Parent directory not found: %s\n", parent_path);
    return -ENOENT;
  }

  struct wfs_inode parent_inode;
  read_inode(&parent_inode, parent_inode_num);
  printf("Read parent inode: mode = %o, nlinks = %d, size = %ld\n",
         parent_inode.mode, parent_inode.nlinks, parent_inode.size);

  if (!S_ISDIR(parent_inode.mode)) {
    printf("Parent is not a directory: %s\n", parent_path);
    return -ENOTDIR;
  }

  struct wfs_dentry *dentry;
  for (int i = 0; i < N_BLOCKS && parent_inode.blocks[i] != 0; i++) {
    off_t block_offset = sb.d_blocks_ptr + parent_inode.blocks[i] * BLOCK_SIZE;
    printf("Checking block %d at offset %ld\n", i, block_offset);
    dentry = (struct wfs_dentry *)((char *)disk_mmap + block_offset);

    for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
      if (dentry[j].num != -1 && strcmp(dentry[j].name, dirname) == 0) {
        printf("Directory already exists: %s\n", path);
        return -EEXIST;
      }
    }
  }

  int inode_num = allocate_free_inode();
  if (inode_num < 0) {
    printf("Failed to allocate inode for directory: %s\n", path);
    return inode_num; // Returns -ENOSPC if no space is available
  }
  printf("Allocated new inode for directory: inode_num = %d\n", inode_num);

  struct wfs_inode new_inode = {
      .mode = mode | S_IFDIR,
      .nlinks = 2,
      .size = 2 * sizeof(struct wfs_dentry),
      .atim = time(NULL),
      .mtim = time(NULL),
      .ctim = time(NULL),
  };
  printf(
      "Initialized new directory inode: mode = %o, nlinks = %d, size = %ld\n",
      new_inode.mode, new_inode.nlinks, new_inode.size);

  int block_num = allocate_free_data_block();
  if (block_num < 0) {
    printf("Failed to allocate data block for directory: %s\n", path);
    return block_num; // Returns -ENOSPC if no space is available
  }
  new_inode.blocks[0] = block_num;
  printf("Allocated data block for directory: block_num = %d\n", block_num);

  printf("Writing new inode to table: inode_num = %d\n", inode_num);
  write_inode(&new_inode, inode_num);

  struct wfs_dentry *new_dir_block =
      (struct wfs_dentry *)((char *)disk_mmap + sb.d_blocks_ptr +
                            block_num * BLOCK_SIZE);
  strncpy(new_dir_block[0].name, ".", MAX_NAME);
  new_dir_block[0].num = inode_num;
  printf("Created '.' entry in new directory: num = %d\n",
         new_dir_block[0].num);

  strncpy(new_dir_block[1].name, "..", MAX_NAME);
  new_dir_block[1].num = parent_inode_num;
  printf("Created '..' entry in new directory: num = %d\n",
         new_dir_block[1].num);

  for (int i = 2; i < BLOCK_SIZE / sizeof(struct wfs_dentry); i++) {
    new_dir_block[i].num = -1;
  }

  int parent_block_num = -1;

  for (int i = 0; i < N_BLOCKS; i++) {
    if (parent_inode.blocks[i] == 0) {
      parent_block_num = allocate_free_data_block();
      if (parent_block_num < 0) {
        printf("Failed to allocate data block for parent directory: %s\n",
               parent_path);
        return parent_block_num;
      }
      parent_inode.blocks[i] = parent_block_num;
      printf("Allocated new data block for parent directory: "
             "parent_block_num = %d\n",
             parent_block_num);
      break;
    } else {
      struct wfs_dentry *parent_dir_block =
          (struct wfs_dentry *)((char *)disk_mmap + sb.d_blocks_ptr +
                                parent_inode.blocks[i] * BLOCK_SIZE);
      for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
        if (parent_dir_block[j].num == -1) {
          parent_dir_block[j].num = inode_num;
          strncpy(parent_dir_block[j].name, dirname, MAX_NAME);
          printf("Added new directory entry to parent: %s\n", dirname);
          write_inode(&parent_inode, parent_inode_num);
          return 0;
        }
      }
    }
  }

  if (parent_block_num != -1) {
    struct wfs_dentry *parent_dir_block =
        (struct wfs_dentry *)((char *)disk_mmap + sb.d_blocks_ptr +
                              parent_block_num * BLOCK_SIZE);
    parent_dir_block[0].num = inode_num;
    strncpy(parent_dir_block[0].name, dirname, MAX_NAME);
    printf("Adding new directory entry to allocated parent block: %s\n",
           dirname);
    write_inode(&parent_inode, parent_inode_num);
  }

  printf("Directory created successfully: %s\n", path);
  return 0;
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
    .mkdir = wfs_mkdir,
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
