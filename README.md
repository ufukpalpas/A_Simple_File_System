# A_Simple_File_System

In this project a very simple file system called sfs is implemented. The file system
has implemented as a library (libsimplefs.a) and stores files in a virtual disk.
The virtual disk is a simple regular Linux file of some certain size.

# File System Specification

The sfs file system has just a single directory, i.e., root directory. No subdirectories are supported. The block size is 4 KB.
Block 0 (first block) contains superblock information (i.e., volume information). 

The next 4 blocks, i.e., blocks 1, 2, 3, 4, contain the bitmap. Bitmap is used to
manage free space. Each bit in the bitmap indicates if the related disk block is free or
used. The next 4 blocks, i.e., blocks 5, 6, 7, 8 contain the root directory. Fixed
sized directory entries are used. Directory entry size is 128 bytes. That means
each disk block can hold 32 directory entries. In this way we can have at most 4x32 =
128 directory entries, hence the file system can store at most 128 files in the disk. Maximum filename is 110 characters long.

A directory entry for a file contains filename and a number to indetify the FCB
(inode) for the file (that can be the index of the FCB in the FCB table). The next 4
blocks, i.e., blocks 9, 10, 11, 12 contain the list of possible FCBs for the files,
i.e., the FCB table (hence the FCB table spans 4 disk blocks). FCB size is 128
bytes. Hence a disk block can contain 32 FCBs. A field in an FCB indicates
whether that FCB is used for a file or not at the moment.

Indexed allocation is used. One level index is for each file. FCB for a
file do not contain any data block number (no pointer in the FCB – this is just for
simplicity). All data block numbers for a file is included in the index node.
Hence there is one index node per file. This limits the size of the file. Max size
for a file can be 4 KB x (4 KB / 4 Bytes) = 4 MB. Disk pointer size is 4 bytes.
That means a block number is 4 bytes (32 bits) long.
