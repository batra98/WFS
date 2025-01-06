### Repository Name:
**WFS: A Custom Filesystem Implementation Using FUSE with RAID Support**

---

### README

# WFS - A Custom Filesystem in Userspace (FUSE)

Welcome to **WFS** – a custom filesystem implementation built using the **FUSE** (Filesystem in Userspace) framework with support for RAID 0 and RAID 1 configurations. This project demonstrates the creation of a block-based filesystem that integrates basic filesystem operations such as file creation, reading, writing, directory management, and file deletion, with additional capabilities for RAID configurations.

### Table of Contents:
- [Introduction](#introduction)
- [Features](#features)
- [Installation](#installation)
- [Usage](#usage)
- [Raid Modes](#raid-modes)
- [Project Structure](#project-structure)

---

### Introduction

This project aims to provide hands-on experience in developing a filesystem from scratch using C and the FUSE framework. It also introduces the concept of RAID (Redundant Array of Independent Disks), offering both RAID 0 (striping) and RAID 1 (mirroring) modes. RAID support ensures improved performance, redundancy, and fault tolerance.

**Key Highlights:**
- Implemented a custom filesystem (WFS) based on FUSE, supporting basic filesystem operations.
- Integrated RAID 0 and RAID 1, providing striped and mirrored data storage for enhanced reliability and performance.
- Simulated filesystem operations like file creation, reading, writing, and deletion, along with directory manipulation.

### Features

- **RAID Support**: Seamless support for RAID 0 (Striping) and RAID 1 (Mirroring), ensuring better performance and fault tolerance.
- **Filesystem Operations**: Implements core filesystem operations including:
  - `getattr` – Fetch file or directory attributes
  - `mknod` – Create files and directories
  - `mkdir` – Create new directories
  - `unlink` – Remove files
  - `rmdir` – Remove directories
  - `read` – Read data from files
  - `write` – Write data to files
  - `readdir` – List the contents of directories
- **File & Directory Management**: Handles creation, reading, and deletion of files and directories.
- **RAID 0 (Striping)**: Distributes data across multiple disks for improved performance.
- **RAID 1 (Mirroring)**: Duplicates data across multiple disks for enhanced fault tolerance and reliability.
- **Filesystem Metadata Mirroring**: Metadata (including superblock and inode data) is mirrored across multiple disks to ensure data integrity.

### Installation

1. Clone the repository:
   ```bash
   git clone https://github.com/yourusername/wfs.git
   cd wfs
   ```

2. Build the project:
   ```bash
   make
   ```

3. Create disk images:
   Use the provided `create_disk.sh` script to create disk images:
   ```bash
   ./create_disk.sh disk1.img
   ./create_disk.sh disk2.img
   ```

4. Make sure all necessary disk images are created before proceeding.

### Usage

1. Create a filesystem:
   ```bash
   ./mkfs -r 1 -d disk1.img -d disk2.img -i 32 -b 200
   ```

   This command creates a filesystem with RAID 1 configuration, 32 inodes, and 200 data blocks, using two disks.

2. Mount the filesystem:
   ```bash
   mkdir mnt
   ./wfs disk1.img disk2.img -f -s mnt
   ```

   This mounts the filesystem at the `mnt` directory.

3. Perform basic filesystem operations:
   - Create a file:
     ```bash
     touch mnt/myfile.txt
     ```
   - Write to the file:
     ```bash
     echo "Hello, WFS!" > mnt/myfile.txt
     ```
   - Read from the file:
     ```bash
     cat mnt/myfile.txt
     ```

4. Unmount the filesystem:
   ```bash
   ./umount.sh mnt
   ```

### Raid Modes

1. **RAID 0 (Striping)**:
   - Provides increased performance by splitting data across multiple disks.
   - Data is written in chunks (512 bytes) to each disk in a round-robin fashion.
   - Use the `-r 0` flag when creating the filesystem:
     ```bash
     ./mkfs -r 0 -d disk1.img -d disk2.img -i 32 -b 200
     ```

2. **RAID 1 (Mirroring)**:
   - Mirrors data across multiple disks for redundancy, ensuring no data loss in case of a disk failure.
   - Use the `-r 1` flag when creating the filesystem:
     ```bash
     ./mkfs -r 1 -d disk1.img -d disk2.img -i 32 -b 200
     ```

3. **RAID 1v (Verified Mirroring)**:
   - A verified version of RAID 1 that compares data across multiple disks during read operations, ensuring data integrity.
   - Use the `-r 1v` flag when creating the filesystem:
     ```bash
     ./mkfs -r 1v -d disk1.img -d disk2.img -i 32 -b 200
     ```

### Project Structure

- **mkfs.c**: Initializes the filesystem, sets up RAID configurations, and writes the superblock and inode information to disk.
- **wfs.c**: Implements the FUSE filesystem, handling operations like file reading, writing, and directory management.
- **wfs.h**: Contains the structure definitions and constants used throughout the filesystem.
- **create_disk.sh**: A helper script to create disk image files.
- **Makefile**: A build script to compile the project.
