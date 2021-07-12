#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include "simplefs.h"
#include <semaphore.h>
#include <pthread.h>


// Global Variables =======================================
int vdisk_fd; // Global virtual disk file descriptor. Global within the library.
              // Will be assigned with the vsfs_mount call.
              // Any function in this file can use this.
              // Applications will not use this directly. 
// ========================================================

struct openT openTable[MAX_OPEN];
int openCount = 0;
int fileCount = 0;
int totalBlock = 0;
int freeBlock = 0;
sem_t* sem;

// read block k from disk (virtual disk) into buffer block.
// size of the block is BLOCKSIZE.
// space for block must be allocated outside of this function.
// block numbers start from 0 in the virtual disk. 
int read_block (void *block, int k)
{
    int n;
    int offset;

    offset = k * BLOCKSIZE;
    lseek(vdisk_fd, (off_t) offset, SEEK_SET);
    n = read (vdisk_fd, block, BLOCKSIZE);
    if (n != BLOCKSIZE) {
	printf ("read error\n");
	return -1;
    }
    return (0); 
}

// write block k into the virtual disk. 
int write_block (void *block, int k)
{
    int n;
    int offset;

    offset = k * BLOCKSIZE;
    lseek(vdisk_fd, (off_t) offset, SEEK_SET);
    n = write (vdisk_fd, block, BLOCKSIZE);
    if (n != BLOCKSIZE) {
	printf ("write error\n");
	return (-1);
    }
    return 0; 
}


/**********************************************************************
   The following functions are to be called by applications directly. 
***********************************************************************/

void bin(unsigned int n)
{
    unsigned int i;
    for (i = 1 << 31; i > 0; i = i / 2)
        (n & i) ? printf("1") : printf("0");
}


int getFCBindex(char* fileName){

	struct directory_entry read[DIR_ENTRY_EACH_BLOCK];
	for(int i = 0; i < DIR_COUNT; i++){
		read_block((void *)read, DIR_START + i);
		for(int j = 0; j < DIR_ENTRY_EACH_BLOCK; j++){
			if(read[j].exists == 1){
				if(strcmp(read[j].name, fileName) == 0){
					return read[j].table_entry;
				}
			}
		}
	}
	return -1;
}

int createFCB(int index){

	struct fcb write[FCB_EACH_BLOCK];
	int diskNo = index/FCB_EACH_BLOCK;
	int offset = index%FCB_EACH_BLOCK;
	read_block((void*) write, FCB_START+diskNo);
	write[offset].isUsed = 1;
	write[offset].indexBlock = findEmptyBlock();
	//printf("indexBlock: %d, diskno: %d, offs: %d\n", write[offset].indexBlock, diskNo, offset);
	int indexBlockPointers[BLOCKSIZE/4];
	read_block((void*) indexBlockPointers, write[offset].indexBlock);
	for(int i = 0; i < BLOCKSIZE/4; i++)
		indexBlockPointers[i] = -1;
	write_block((void *) indexBlockPointers, write[offset].indexBlock);
	write[offset].size = 0;
	write_block((void*)write, FCB_START + diskNo);
	return -1;
}

void unsetBitMap(int block){
	int diskNo = block/BITMAP_INT_PER_BLOCK;
	int intNo = (block - diskNo*BITMAP_INT_PER_BLOCK)/BITMAP_INT_PER_BLOCK;
	int offset = (block - diskNo*BITMAP_INT_PER_BLOCK)%BITMAP_INT_PER_BLOCK;
	//printf("dsk: %d, %d, %d\n", diskNo, intNo, offset);

	char superBlock[BLOCKSIZE];
	read_block((void *)superBlock, SUPERBLOCK_START);
	*((int *)(superBlock + 4)) += 1;
	write_block((void *)superBlock, SUPERBLOCK_START);

	unsigned int bitmap[BITMAP_INT_PER_BLOCK];
	read_block((void*)bitmap, BITMAP_START + diskNo);
	bitZero(bitmap, intNo *32 + offset);
	write_block((void*)bitmap, BITMAP_START + diskNo);
}

void setBitMap(int block){
	int diskNo = block/BITMAP_INT_PER_BLOCK;
	int intNo = (block - (diskNo*BITMAP_INT_PER_BLOCK))/BITMAP_INT_PER_BLOCK;//0
	int offset = (block - (diskNo*BITMAP_INT_PER_BLOCK))%BITMAP_INT_PER_BLOCK;//0
//	printf("dsk: %d, %d, %d\n", diskNo, intNo, offset);
	
	char superBlock[BLOCKSIZE];
	read_block((void *)superBlock, SUPERBLOCK_START);
	*((int *)(superBlock + 4)) -= 1;
	write_block((void *)superBlock, SUPERBLOCK_START);
	
	unsigned int bitmap[BITMAP_INT_PER_BLOCK];
	read_block((void*)bitmap, BITMAP_START + diskNo);
	bitOne(bitmap, intNo *32 + offset);
//	printf("bin:  %d\n", bitTest(bitmap, intNo *32 + offset));
	write_block((void*)bitmap, BITMAP_START + diskNo);
}

int deleteFCB(int index){

	struct fcb write[FCB_EACH_BLOCK];
	int diskNo = index/FCB_EACH_BLOCK;
	int offset = index%FCB_EACH_BLOCK;
	read_block((void *) write, diskNo + FCB_START);
	write[offset].isUsed = 0;
	//printf("indexBlock: %d, diskno: %d, offs: %d index: %d\n", write[offset].indexBlock, diskNo, offset, index);
	int indexBlockPointers[BLOCKSIZE/4];
	read_block((void *)indexBlockPointers, write[offset].indexBlock);
	
	for(int i = 0; i < BLOCKSIZE/4; i++){
		if(indexBlockPointers[i] != -1){
			//printf("indexblock: %d\n", indexBlockPointers[i]);
			unsetBitMap(indexBlockPointers[i]);
			char null[BLOCKSIZE];
			memset(null, (NULL), BLOCKSIZE);
			write_block((void *)null, indexBlockPointers[i]);
		}
		indexBlockPointers[i] = NULL;
	}
	write_block((void *) indexBlockPointers, write[offset].indexBlock);
	unsetBitMap(write[offset].indexBlock);
	write[offset].size = 0;
	write[offset].indexBlock = -1;
	write_block((void*)write, FCB_START + diskNo);
	return -1;
}

int findEmptyBlock(){

	unsigned int bitmap[BITMAP_INT_PER_BLOCK];
	int block = 0;
	for(int i = 0; i < BITMAP_COUNT; i++){
		read_block((void*)bitmap, BITMAP_START + i);
		for(int j = 0; j < 32; j++){
			for(int k = 0; k < 32; k++){
			//bin(bitmap[j]);
				if(bitTest(bitmap, (k + j*32)) == 0){
					block = k + j*32 + i*BITMAP_INT_PER_BLOCK;
					//printf("bmt: %d, %d, %d\n", block, j, k);
					setBitMap(block);
					
					return block;
				}
				//printf("j : %d\n", j);
			}
		}
	}
	//printf("NO EMPTY BLOCK IN BITMAP\n");
	return -1;
}


int findEmptyFCB(){
	struct fcb read[FCB_EACH_BLOCK];
	for(int i = 0; i < FCB_COUNT; i++){
		read_block((void *)read, FCB_START + i);
		for(int j = 0; j < FCB_EACH_BLOCK; j++){
			if(read[j].isUsed == 0){
				//printf("empty fcb entry: %d, %d\n", i, j);
				return FCB_EACH_BLOCK*i + j;
			}
		}
	}
	printf("NO EMPTY FCB\n");
	return -1;
}

// this function is partially implemented.
int create_format_vdisk (char *vdiskname, unsigned int m)
{
    char command[1000];
    int size;
    int num = 1;
    int count;
    size  = num << m;
    count = size / BLOCKSIZE;
    printf ("%d %d\n", m, size);
    sprintf (command, "dd if=/dev/zero of=%s bs=%d count=%d",
             vdiskname, BLOCKSIZE, count);
    printf ("executing command = %s\n", command);
    system (command);
    // now write the code to format the disk below.
    int blockCount = count - (SUPERBLOCK_COUNT + BITMAP_COUNT + DIR_COUNT + FCB_COUNT);
   // printf("blockCount: %d\n", blockCount);
    vdisk_fd = open(vdiskname, O_RDWR);
    //init semaphore
	sem_unlink ("sem"); 

	/* create and initialize the semaphores */
	sem = sem_open("sem", O_RDWR | O_CREAT, 0660, 1);
	if (sem < 0) {
		perror("can not create semaphore\n");
		exit (1); 
	}
	//init superblock
    char toWrite[BLOCKSIZE];
    *((int *)(toWrite)) = blockCount;     // # of total blocks 
    *((int *)(toWrite + 4)) = blockCount; // # of free blocks
    *((int *)(toWrite + 8)) = 0;        // # of files in the disk
    *((int *)(toWrite + 12)) = 0;        // # of directory entries
    write_block((void*)toWrite, SUPERBLOCK_START);
    
    //init bitMap
    unsigned int bitmap[BITMAP_INT_PER_BLOCK]; // bitwise operations will be used on ints
    for(int i = 0; i < BITMAP_INT_PER_BLOCK; i++)
    	bitmap[i] = 0;
    for(int i = 0; i < 13; i++)
		bitOne(bitmap, i); // superblock, bitmap, root directory and fcb table are always used, not available for f,le allocation
    write_block((void*)bitmap, BITMAP_START);
    for(int i = 0; i < 13; i++)
		bitZero(bitmap, i);
    
    for(int i = 1; i < BITMAP_COUNT; i++)
    	write_block((void*)bitmap, BITMAP_START + i);
   // bitOne(bitmap, 13); // entry*32 + index
    /*printf("test: %d\n", (bitTest(bitmap,13) != 0));
    bitOne(bitmap, 35);
    bitOne(bitmap, 34);
    bin(bitmap[1]);*/
    
    //init root direvtory
    struct directory_entry root[DIR_ENTRY_EACH_BLOCK];
    for(int i = 0; i < DIR_COUNT; i++)
    	write_block((void *)root, DIR_START + i);
    //init FCB table
    struct fcb table[FCB_EACH_BLOCK];
    for(int i = 0; i < FCB_COUNT; i++)
    	write_block((void *)table, FCB_START + i);
    
    // .. your code...
    
   // printf("size: %d, %d, %d\n",sizeof(struct directory_entry), sizeof(struct fcb), sizeof(unsigned int[BITMAP_INT_PER_BLOCK]));
    return (0); 
}


// already implemented
int sfs_mount (char *vdiskname)
{
    // simply open the Linux file vdiskname and in this
    // way make it ready to be used for other operations.
    // vdisk_fd is global; hence other function can use it. 
    sem = sem_open("sem", O_RDWR);
	sem_wait(sem);
	vdisk_fd = open(vdiskname, O_RDWR);
    if(vdisk_fd == -1){
    	sem_post(sem);	
	return -1;
    }
    char read[BLOCKSIZE];//acquire superblock attributes
    read_block((void *)read, SUPERBLOCK_START);
    totalBlock = ((int *)(read))[0];
    freeBlock = ((int *)(read + 4))[0];
    fileCount = ((int *)(read + 8))[0];
    
    for(int i = 0; i < MAX_OPEN; i++)
    	openTable[i].isOpen = 0;
    openCount = 0;
    return(0);
}


// already implemented
int sfs_umount ()
{
    fsync(vdisk_fd); // copy everything in memory to disk
    if (close(vdisk_fd) == -1){
	
    	return -1;
	}
	sem_post(sem);
	//printf("sem post sonrası\n");
	sem_close(sem);
    return (0); 
}


int sfs_create(char *filename)
{
	
	struct directory_entry read[DIR_ENTRY_EACH_BLOCK];
	for(int i = 0; i < DIR_COUNT; i++){
		read_block((void *)read, DIR_START + i);
		for(int j = 0; j < DIR_ENTRY_EACH_BLOCK; j++){
			if(read[j].exists == 1){
				if(strcmp(read[j].name, filename) == 0){
					printf("already exists\n");
					return -1;
				}
			}
		}
	}
	
	int dir_block, dir_entry;
	bool flag = 0;
	for(int i = 0; i < DIR_COUNT; i++){
		read_block((void *)read, DIR_START + i);
		for(int j = 0; j < DIR_ENTRY_EACH_BLOCK; j++){
			if(read[j].exists == 0){
				//printf("fnd = %d, %d\n", i ,j);
				dir_block = i;
				dir_entry = j;
				flag = 1;
				break;
			}
		}
		if(flag)
			break;
	}
	
	
	if(!flag){
		printf("disk is full");
		return -1;
	}
	
	char superBlock[BLOCKSIZE];
	read_block((void *)superBlock, SUPERBLOCK_START);
	*((int *)(superBlock + 8)) += 1;
	*((int *)(superBlock + 12)) += 1;
	write_block((void *)superBlock, SUPERBLOCK_START);
	
	strcpy(read[dir_entry].name, filename);
	char nu = '\0';
	memcpy((read[dir_entry].name + 110 ), &nu, sizeof(char));
	read[dir_entry].table_entry = findEmptyFCB();// kontrol et !!!!!!!!
	createFCB(read[dir_entry].table_entry);
	read[dir_entry].exists = 1;
	write_block((void*)read, DIR_START + dir_block);
	
	fileCount++;
	printf("File \"%s\" is succesfully created\n", filename);
	return (0);
}


int sfs_open(char *file, int mode)
{
	if (openCount >= 16)
	{
		printf("Maximum # of files are open\n");
		return -1;
	}
	
	int fd = -1;
	bool flag = 0;
	for (int i = 0; i < MAX_OPEN; i++)
	{
		if(openTable[i].isOpen == 1){
			if(strcmp(file, openTable[i].name) == 0){
				printf("File s already open\n");
				return -1;
			}
		}else{
			flag = 1;
			fd = i;
			break;
		}
	}
	if(!flag){
		printf("File doesn't exists\n");
		return -1;
	}
	struct directory_entry read[DIR_ENTRY_EACH_BLOCK];
	for(int i = 0; i < DIR_COUNT; i++){
		read_block((void *)read, DIR_START + i);
		for(int j = 0; j < DIR_ENTRY_EACH_BLOCK; j++){
			if(read[j].exists == 1 && strcmp(read[j].name, file) == 0){
				//printf("found the file at %d, %d, %d\n", i, j, fd);
				openTable[fd].isOpen = 1;
				openTable[fd].openMode = mode;
				openTable[fd].name = file;
				openCount++;
				printf("File \"%s\" is succesfully opened\n", openTable[fd].name);
				return fd;
			}
		}
	}
	printf("Maximum number of files are open in the system\n");
	return -1;
}

int sfs_close(int fd){
	if (openCount <= 0)
	{
		printf("There isn't any file with fd %d\n", fd);
		return -1;
	}
	
	if(fd < 0 || fd > 16){
		printf("File descriptor isn't valid\n");
		return -1;
	}
	
	if(openTable[fd].isOpen == 0){
		printf("File is already closed\n");
	}

	openTable[fd].isOpen = 0;
	openTable[fd].openMode = -1;
	printf("File \"%s\" is succesfully closed\n", openTable[fd].name);
	openTable[fd].name = NULL;
	openCount--;
	
	return 0;
}

int sfs_getsize (int  fd)
{
	if(openTable[fd].isOpen == 0){
		printf("File is not open");
		return -1;
	}
	
	int index = getFCBindex(openTable[fd].name);
	//printf("Table entry: %d\n", index);
	struct fcb read[FCB_EACH_BLOCK];
	read_block((void *)read, FCB_START + index/FCB_EACH_BLOCK);
	if(read[index%FCB_EACH_BLOCK].isUsed != 1){
		printf("fcb is not vali\n");
		return -1;
	}
	return read[index%FCB_EACH_BLOCK].size;
}

int sfs_read(int fd, void *buf, int n){

	if(fd < 0 || fd > 16){
		printf("Invalid fd\n");
		return -1;
	}

	if(openTable[fd].openMode == 1){
		printf("Mode error\n");
		return -1;
	}

	//printf("fd: %d\n", fd);
	int file_size = sfs_getsize(fd);
	if(file_size == 0){
		printf("File is empty\n");
		return -1;
	}
	if(n > file_size){ // 4MB
		printf("Requested size is more than the file size, the program will give you %d bytes of data\n", file_size);
		n = file_size;
	}

	
	int index = getFCBindex(openTable[fd].name);
	//printf("Table entry: %d\n", index);
	
	struct fcb fileControlBlock[FCB_EACH_BLOCK];
	read_block((void *)fileControlBlock, FCB_START + index/FCB_EACH_BLOCK);
	int indexBlockAddress = fileControlBlock[index%FCB_EACH_BLOCK].indexBlock;
	//printf("indexBlock: %d\n", indexPoint);
	
	int indexBlockPointers[BLOCKSIZE/4];
	read_block((void *)indexBlockPointers, indexBlockAddress);

	
	//printf("blokc: %d\n", blockOffset);
	int pointer, size = n;
	int loopCount = 0;
	int dataOffset = n%BLOCKSIZE; // end point of the read
	int blockOffset = n/BLOCKSIZE; // # of blocks to read
	do{
		if(loopCount >0)
			size -= BLOCKSIZE;
		pointer = indexBlockPointers[loopCount];
		char read[BLOCKSIZE];
		read_block(read, pointer);
	//	printf("buf:  %d, %d, %d, %d\n", dataOffset, blockOffset, pointer, size);
		for(int i = 0; i < size; i++){
		//	printf("loopCount: %d, %c\n", loopCount, *(read + i));
			*((char *)(buf + i + loopCount*BLOCKSIZE)) = *(read + i);
			if(i == BLOCKSIZE -1){
				break;
			}
		}
		if(blockOffset == loopCount)
			*((char *)(buf + dataOffset + loopCount*BLOCKSIZE)) = '\0';
			
		loopCount++;
		
	}while(size > BLOCKSIZE);
	//printf("%d bytes of data read to the file %s\n", n, openTable[fd].name);
	return (0); 
}


int sfs_append(int fd, void *buf, int n)
{
	if(fd < 0 || fd > 16){
		printf("Invalid fd\n");
		return -1;
	}

	if(openTable[fd].isOpen == 0){
		printf("file is not open\n");
		return -1;
	}
	
	if(openTable[fd].openMode == 0){
		printf("Mode error\n");
		return -1;
	}

	//printf("fd: %d\n", fd);
	int file_size = sfs_getsize(fd);
	if(file_size >= 4194304){ // 4MB
		printf("File is full\n");
		return -1;
	}else if(file_size + n > 4190208){
		printf("Oversize\n");
		return -1;
	}else if(n > freeBlock * BLOCKSIZE){
		printf("Not Enough Space\n");
		return -1;
	}

	int index = getFCBindex(openTable[fd].name);
	//printf("Table entry: %d\n", index);
	
	struct fcb read[FCB_EACH_BLOCK];
	read_block((void *)read, FCB_START + index/FCB_EACH_BLOCK);
	int indexPoint = read[index%FCB_EACH_BLOCK].indexBlock;
	//printf("indexBlock: %d\n", indexPoint);
	
	int indexBlockPointers[BLOCKSIZE/4];
	read_block((void *)indexBlockPointers, indexPoint);
//	printf("indexPoint: %d, ilk eleman: %d\n", indexPoint, indexBlockPointers[0]);
	
	int pointer, size = n;
	int loopCount = 0, old = 0;
	bool isCrossed = 0, wasCrossed = 0;
	do{
		
		file_size = sfs_getsize(fd);
		int dataOffset = file_size%BLOCKSIZE;
		int blockOffset = file_size/BLOCKSIZE;
//		if(isCrossed)
//			size -= remaining;
		int remaining = BLOCKSIZE - dataOffset;
		
		pointer = indexBlockPointers[blockOffset];
		if(dataOffset == 0){
			pointer = findEmptyBlock();
		}
			
		if(remaining < size){
			isCrossed = 1;
			old = remaining;
		}else
			isCrossed = 0;
		//printf("yazılcak yer : %d, %d, %d, %d, %d\n", pointer, blockOffset, dataOffset, remaining, size);
		char readTo[BLOCKSIZE];
		read_block((void *)readTo, pointer);
		if(wasCrossed == 1){
			//printf("read: %d, %d\n", sizeof(buf), n);
			for(int i = 0; i < size; i++){
				*(readTo + dataOffset + i) = *((char *)(buf + i + loopCount*old));
				read[index%FCB_EACH_BLOCK].size += 1;
				if(i == remaining -1){
					//*(readTo + dataOffset + i) = '\0';
					break;
				}
			}
			//*(readTo + dataOffset + size+1) = '\0';
		} else {
			for(int i = 0; i < size; i++){
				*(readTo + dataOffset + i) = *((char *)(buf + i + loopCount*remaining));
				read[index%FCB_EACH_BLOCK].size += 1;
				if(i == remaining -1){
				//	*(readTo + dataOffset + i+1) = '\0';
					break;
				}
			}
			//*(readTo + dataOffset + remaining+1) = '\0';
		}
		write_block(readTo, pointer);
		indexBlockPointers[blockOffset] = pointer;		
		//printf("File size: %d\n", read[index%FCB_EACH_BLOCK].size);
		write_block((void *)read, FCB_START + index/FCB_EACH_BLOCK);
		if(isCrossed){
			size -= old;
			wasCrossed = 1;
		}else
			size -= remaining;
		loopCount++;
	}while(isCrossed);
	
	write_block((void *)indexBlockPointers, indexPoint);
	//printf("%d bytes of data appended to the file %s\n", n, openTable[fd].name);
	return (0); 
}

int sfs_delete(char *filename)
{
	bool flag = 0;
	int dir_block, dir_entry;
	struct directory_entry DIR[DIR_ENTRY_EACH_BLOCK];
	for(int i = 0; i < DIR_COUNT; i++){
		read_block((void *)DIR, DIR_START + i);
		for(int j = 0; j < DIR_ENTRY_EACH_BLOCK; j++){
			if(DIR[j].exists == 1){
				if(strcmp(DIR[j].name, filename) == 0){
					dir_block = i;
					dir_entry = j;
					//printf("Found the file\n");
					flag = 1;
					break;
				}
			}
		}
		if(flag)
			break;
	}
	if(!flag){
		printf("File \"%s\"doesn't exists\n", filename);
		return -1;
	}

	char superBlock[BLOCKSIZE];
	read_block((void *)superBlock, SUPERBLOCK_START);
	*((int *)(superBlock + 8)) -= 1;
	*((int *)(superBlock + 12)) -= 1;
	write_block((void *)superBlock, SUPERBLOCK_START);

	//read_block((void *)DIR, DIR_START + dir_block);
	//printf("table ent: %d\n", DIR[dir_entry].table_entry);
	deleteFCB(DIR[dir_entry].table_entry);
	strcpy(DIR[dir_entry].name, "");
	DIR[dir_entry].table_entry = -1;// kontrol et !!!!!!!!
	DIR[dir_entry].exists = 0;
	write_block((void*)DIR, DIR_START + dir_block);
	
	fileCount--;
	printf("File \"%s\" is succesfully deleted\n", filename);
	return (0); 
}



void printDisk()
{
    char block[BLOCKSIZE];
    read_block((void *)block, 0);
    printf("\nSuperblock:\n");
    printf("\t# of data blocks: %d\n", *((int *)(block)));
    printf("\t# of free blocks %d\n", *((int *)(block + 4)));
    printf("\t# of files in disk: %d\n", *((int *)(block + 8)));
    printf("\t# of directory entries in disk: %d\n", *((int *)(block + 8)));
    
    
    printf("\nBitmap:\n");
    int blockToRead[BLOCKSIZE/4];
    for(int i = 0; i < 4; i++){
    	read_block((void *)blockToRead, i+1);
    	for(int j = 0; j < BITMAP_INT_PER_BLOCK; j++){
    		printf("\t\t");
    		bin(blockToRead[j]);
    		printf("\n");
    	}
    }
    
    printf("\nRoot Directory:\n");
    struct directory_entry readDir[DIR_ENTRY_EACH_BLOCK];
    for(int i = 5; i < 9; i++){
    	read_block((void *)readDir, i);
    	for(int j = 0; j < DIR_ENTRY_EACH_BLOCK; j++){
    		printf("\t");
    		if(readDir[j].exists == 0)
    			printf("-1\n");
    		else
    			printf("Name: %s, table_entry: %d\n", readDir[j].name, readDir[j].table_entry);
    	}
    }
    
    printf("\nFCB Table:\n");
    struct fcb readFCB[FCB_EACH_BLOCK];
    for(int i = 9; i < 13; i++){
    	read_block((void *)readFCB, i);
    	for(int j = 0; j < FCB_EACH_BLOCK; j++){
    		printf("\t");
    		if(readFCB[j].isUsed == 0)
    			printf("-1\n");
    		else{
    			printf("Size: %d, index_block address: %d\n\t\tIndex Block:\n", readFCB[j].size, readFCB[j].indexBlock);
    			read_block((void *)blockToRead, readFCB[j].indexBlock);
    			for(int k = 0; k < 1024; k++){
    				if(blockToRead[k] != -1)
    					printf("\t\t\tAddress: %d\n", blockToRead[k]);
    			}
    			printf("\t\t\tEnd of Index Block\n");
    		}
    	}
    }
}
