

// Do not change this file //

#define MODE_READ 0
#define MODE_APPEND 1
#define BLOCKSIZE 4096 // bytes
#define MAX_FILE 128
#define FILENAME_LEN 110
#define FILE_PER_PROCESS 16
#define DIR_ENTRY_SIZE 128   // byte
#define FCB_SIZE 128 // byte
#define SUPERBLOCK_START 0
#define SUPERBLOCK_END 1
#define SUPERBLOCK_COUNT 1
#define BITMAP_START 1
#define BITMAP_END 5
#define BITMAP_COUNT 4
#define DIR_START 5
#define DIR_END 9
#define DIR_COUNT 4
#define FCB_START 9
#define FCB_END 13
#define FCB_COUNT 4
#define DATA_START 13
#define BITMAP_INT_PER_BLOCK 1024
#define DIR_ENTRY_EACH_BLOCK 32
#define FCB_EACH_BLOCK 32
#define MAX_OPEN 16
#define bitOne(A,k) ( A[(k/32)] |= (1 << (k%32)) )
#define bitZero(A,k) ( A[(k/32)] &= ~(1 << (k%32)) )
#define bitTest(A,k) ( A[(k/32)] & (1 << (k%32)) ) 

struct directory_entry{	
	int table_entry;
	int exists;
	char name[110];
	char empty[8];

};

struct fcb{	
	int indexBlock;
	int size;
	char empt[118];
	short int isUsed;
};

struct openT{
	int isOpen;
    int openMode;
	char* name;
};

int create_format_vdisk (char *vdiskname, unsigned int  m);

int sfs_mount (char *vdiskname);

int sfs_umount ();

int sfs_create(char *filename);

int sfs_open(char *filename, int mode);

int sfs_close(int fd);

int sfs_getsize (int fd);

int sfs_read(int fd, void *buf, int n);

int sfs_append(int fd, void *buf, int n);

int sfs_delete(char *filename);

void printDisk();

int findEmptyBlock();

void unsetBitMap(int block);

void setBitMap(int block);


