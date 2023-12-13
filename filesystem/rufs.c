#define FUSE_USE_VERSION 26

#include <linux/stat.h>
#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>
#include <stdint.h>

#include "block.h"
#include "rufs.h"

char diskfile_path[PATH_MAX];

// Declare your in-memory data structures here
struct superblock *sb;
bitmap_t inode_bitmap;
bitmap_t data_bitmap;

// global variable to keep track of the size of the inode bitmap
size_t inode_bitmap_size = MAX_INUM / 8;
// global variable to keep track of the size of the data bitmap
size_t data_bitmap_size = MAX_DNUM / 8;

// global variable to keep track of the log file
FILE* log_file = NULL;

// create a function that returns an array of tokens of a string
char** tokenize(const char* str, const char* delimiter, size_t* num_of_tokens){
	// allocate an array of exactly the right size by using strtok, malloc, and realloc
	char** tokens = NULL;
	char* string = strdup(str);
	char* token = strtok(string, delimiter);
	*num_of_tokens = 0;

	while (token != NULL) {
		// if tokens is NULL, use malloc, else use realloc
		if (tokens == NULL) {
			tokens = (char**)malloc(sizeof(char*));
		} else {
			tokens = (char**)realloc(tokens, (*num_of_tokens + 1) * sizeof(char*));
		}

		// use strdup to copy the token into the array
		tokens[*num_of_tokens] = strdup(token);
		*num_of_tokens += 1;

		token = strtok(NULL, delimiter);
	}

	free(string);

	return tokens;
}

// create a function that frees the tokens
void free_tokens(char** tokens, size_t num_of_tokens) {
	for (size_t i = 0; i < num_of_tokens; i++) {
		free(tokens[i]);
	}

	free(tokens);
}

/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {
	// write to the log file that get_avail_ino() is called
	fprintf(log_file, "get_avail_ino() is called\n");

	// Step 1: Read inode bitmap from disk
	
	// Step 2: Traverse inode bitmap to find an available slot
	// use get_bitmap() to check the availability of a bitmap slot
	for (int i = 0; i < inode_bitmap_size; i++) {
		for (int j = 0; j < 8; j++) {
			if (!get_bitmap(inode_bitmap, i * 8 + j)) {
				// Step 3: Update inode bitmap and write to disk 
				set_bitmap(inode_bitmap, i * 8 + j);
				bio_write(1, (void*)inode_bitmap);
				return i * 8 + j;
			}
		}
	}

	// Step 3: Update inode bitmap and write to disk 

	return -1;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {
	// write to the log file that get_avail_blkno() is called
	fprintf(log_file, "get_avail_blkno() is called\n");

	// Step 1: Read data block bitmap from disk

	
	// Step 2: Traverse data block bitmap to find an available slot
	// use get_bitmap() to check the availability of a bitmap slot
	for (int i = 0; i < data_bitmap_size; i++) {
		for (int j = 0; j < 8; j++) {
			if (!get_bitmap(data_bitmap, i * 8 + j)) {
				// Step 3: Update data block bitmap and write to disk 
				set_bitmap(data_bitmap, i * 8 + j);
				bio_write(2, (void*)data_bitmap);
				return i * 8 + j;
			}
		}
	}

	// Step 3: Update data block bitmap and write to disk 

	return -1;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {
	// write to the log file that readi() is called
	fprintf(log_file, "readi(%" PRIu16 ") is called\n", ino);

	// Step 1: Get the inode's on-disk block number
	// Note: add 3 to the inode number because the first three blocks are reserved for superblock, inode bitmap and data bitmap
	uint32_t block_num = sb->i_start_blk + (ino / (BLOCK_SIZE / sizeof(struct inode)));

	// Step 2: Get offset of the inode in the inode on-disk block
	uint32_t offset = (ino % (BLOCK_SIZE / sizeof(struct inode))) * sizeof(struct inode);

	// Step 3: Read the block from disk and then copy into inode structure
	// read the inode block from disk
	void* inode_block = malloc(BLOCK_SIZE);
	bio_read(block_num, inode_block);

	// copy the inode from the inode block
	memcpy(inode, inode_block + offset, sizeof(struct inode));

	return 0;
}

int writei(uint16_t ino, struct inode *inode) {
	// write to the log file that writei() is called

	fprintf(log_file, "writei(%" PRIu16 ") is called\n", ino);


	// Step 1: Get the block number where this inode resides on disk
	// Note: add 3 to the inode number because the first three blocks are reserved for superblock, inode bitmap and data bitmap
	uint32_t block_num = sb->i_start_blk + (ino / (BLOCK_SIZE / sizeof(struct inode)));
	
	// Step 2: Get the offset in the block where this inode resides on disk
	uint32_t offset = (ino % (BLOCK_SIZE / sizeof(struct inode))) * sizeof(struct inode);

	// Step 3: Write inode to disk
	// read the inode block from disk
	void* inode_block = malloc(BLOCK_SIZE);
	bio_read(block_num, inode_block);

	// copy the inode into the inode block
	memcpy(inode_block + offset, inode, sizeof(struct inode));

	// write the inode block back to disk
	bio_write(block_num, inode_block);

	// Step 4: Update inode bitmap
	set_bitmap(inode_bitmap, ino);

	// Step 5: Write the bitmap block back to disk
	bio_write(1, (void*)inode_bitmap);

	return 0;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {
	// write to the log file that dir_find() is called
	fprintf(log_file, "dir_find(%" PRIu16 ", '%s') is called\n", ino, fname);

	// Step 1: Call readi() to get the inode using ino (inode number of current directory)
	struct inode inode;
	readi(ino, &inode);

	// check if the inode type is a file
	if (inode.type == 0) {
		return -1;
	}

	// Step 2: Get data block of current directory from inode
	size_t num_of_entries = inode.size / sizeof(struct dirent);

	// Step 3: Read directory's data block and check each directory entry.
	//If the name matches, then copy directory entry to dirent structure
	size_t num_of_data_blocks = num_of_entries / (BLOCK_SIZE / sizeof(struct dirent));

	size_t num_of_entries_in_last_block = num_of_entries % (BLOCK_SIZE / sizeof(struct dirent));

	if (num_of_entries == 0) {
		fprintf(log_file, "dir_find() has returned -1\n");
		return -1;
	}

	if (num_of_entries_in_last_block == 0) {
		num_of_data_blocks -= 1;
		num_of_entries_in_last_block = BLOCK_SIZE / sizeof(struct dirent);
	}

	fprintf(log_file, "inode size: %" PRId32 ", num of entries: %zu, last data block index: %zu, num of entries in last block: %zu\n", inode.size, num_of_entries, num_of_data_blocks, num_of_entries_in_last_block);

	void* data_block = malloc(BLOCK_SIZE);
	for (size_t i = 0; i < num_of_data_blocks + 1; i++) {
		bio_read(sb->d_start_blk + inode.direct_ptr[i], data_block);

		fprintf(log_file, "data block number: %d\n", inode.direct_ptr[i]);

		if (i == num_of_data_blocks) {
			for (size_t j = 0; j < num_of_entries_in_last_block; j++) {
				struct dirent* entry = (struct dirent*)(data_block + j * sizeof(struct dirent));
				fprintf(log_file, "entry name: '%s', fname: '%s'\n", entry->name, fname);
				if ((strcmp(entry->name, fname) == 0)) {
					if (dirent != NULL) {
						memcpy(dirent, entry, sizeof(struct dirent));
					}
					free(data_block);
					return 0;
				}
			}
		} else {
			for (size_t j = 0; j < BLOCK_SIZE / sizeof(struct dirent); j++) {
				struct dirent* entry = (struct dirent*)(data_block + j * sizeof(struct dirent));
				if (strcmp(entry->name, fname) == 0) {
					if (dirent != NULL) {
						memcpy(dirent, entry, sizeof(struct dirent));
					}
					free(data_block);
					return 0;
				}
			}
		}
	}

	free(data_block);
	fprintf(log_file, "dir_find() has returned -1\n");
	return -1;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {
	// write to the log file that dir_add() is called
	fprintf(log_file, "dir_add('%s') is called\n", fname);

	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode
	// use dir_find() to check whether fname exist in dir_inode
	
	if (dir_find(dir_inode.ino, fname, name_len, NULL) == 0) {
		return -1;
	}
	
	// Step 2: Check if fname (directory name) is already used in other entries

	// Step 3: Add directory entry in dir_inode's data block and write to disk
	size_t num_of_entries = dir_inode.size / sizeof(struct dirent);

	size_t num_of_data_blocks = num_of_entries / (BLOCK_SIZE / sizeof(struct dirent));

	size_t num_of_entries_in_last_block = num_of_entries % (BLOCK_SIZE / sizeof(struct dirent));

	// go the the last data block of the directory and add a new entry
	// if the last block is full, then allocate a new data block
	if (num_of_entries_in_last_block == 0) {
		num_of_entries_in_last_block = BLOCK_SIZE / sizeof(struct dirent);
	}

	if (num_of_entries_in_last_block == BLOCK_SIZE / sizeof(struct dirent)) {
		dir_inode.direct_ptr[num_of_data_blocks] = get_avail_blkno();
		dir_inode.size += sizeof(struct dirent);

		// write the updated inode to disk
		writei(dir_inode.ino, &dir_inode);

		// write the new data block to disk
		void* data_block = malloc(BLOCK_SIZE);
		memset(data_block, 0, BLOCK_SIZE);

		struct dirent* entry = (struct dirent*)data_block;
		entry->ino = f_ino;
		entry->valid = 1;
		strncpy(entry->name, fname, name_len);
		entry->len = name_len;

		bio_write(sb->d_start_blk + dir_inode.direct_ptr[num_of_data_blocks], data_block);

		fprintf(log_file, "data block number: %d\n", dir_inode.direct_ptr[num_of_data_blocks]);

		free(data_block);

		fprintf(log_file, "dir_add() returns 0\n");
		return 0;
	}

	// Allocate a new data block for this directory if it does not exist

	// Update directory inode

	// Write directory entry

	// add a new directory entry to the last data block of the directory
	void* data_block = malloc(BLOCK_SIZE);
	bio_read(sb->d_start_blk + dir_inode.direct_ptr[num_of_data_blocks], data_block);

	struct dirent* entry = (struct dirent*)(data_block + num_of_entries_in_last_block * sizeof(struct dirent));
	entry->ino = f_ino;
	entry->valid = 1;
	strncpy(entry->name, fname, name_len);
	entry->len = name_len;
	dir_inode.size += sizeof(struct dirent);

	// write the updated inode to disk
	writei(dir_inode.ino, &dir_inode);

	bio_write(sb->d_start_blk + dir_inode.direct_ptr[num_of_data_blocks], data_block);

	free(data_block);

	return 0;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

	// OPTIONAL

	return 0;
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {

	// write to the log file that get_node_by_path() is called
	fprintf(log_file, "get_node_by_path('%s', %" PRIu16 ") is called\n", path, ino);
	
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way

	size_t num_of_tokens = 0;

	char** tokens = tokenize(path, "/", &num_of_tokens);

	if (tokens == NULL) {
		// copy the inode to the inode pointer
		readi(0, inode);
		return 0;
	}

	struct inode current_inode;
	readi(ino, &current_inode);

	for (size_t i = 0; i < num_of_tokens; i++) {
		char* token = tokens[i];
		struct dirent entry;
		if (dir_find(current_inode.ino, token, strlen(token), &entry) == 0) {
			readi(entry.ino, &current_inode);
		} else {
			fprintf(log_file, "get_node_by_path() returned -1\n");

			// free the tokens
			free_tokens(tokens, num_of_tokens);

			return -1;
		}
	}

	// copy the inode to the inode pointer
	memcpy(inode, &current_inode, sizeof(struct inode));

	// free the tokens
	free_tokens(tokens, num_of_tokens);
	
	fprintf(log_file, "get_node_by_path() returned 0\n");
	return 0;
}

/* 
 * Make file system
 */
int rufs_mkfs() {
	// write to the log file that rufs_mkfs() is called
	fprintf(log_file, "rufs_mkfs() is called\n");

	// Call dev_init() to initialize (Create) Diskfile
	dev_init(diskfile_path);

	// write superblock information
	void* sb_block = malloc(BLOCK_SIZE);
	memset(sb_block, 0, BLOCK_SIZE);
	sb = (struct superblock *)sb_block;

	sb->magic_num = MAGIC_NUM;
	sb->max_inum = MAX_INUM;
	sb->max_dnum = MAX_DNUM;
	sb->i_bitmap_blk = 1;
	sb->d_bitmap_blk = 2;
	sb->i_start_blk = 3;
	sb->d_start_blk = 4 + (MAX_INUM / (BLOCK_SIZE / sizeof(struct inode)));


	// initialize inode bitmap
	void* inode_bitmap_block = malloc(BLOCK_SIZE);
	memset(inode_bitmap_block, 0, BLOCK_SIZE);
	inode_bitmap = (bitmap_t)inode_bitmap_block;

	// initialize data block bitmap
	void* data_bitmap_block = malloc(BLOCK_SIZE);
	memset(data_bitmap_block, 0, BLOCK_SIZE);
	data_bitmap = (bitmap_t)data_bitmap_block;

	// update bitmap information for root directory
	set_bitmap(inode_bitmap, 0);
	set_bitmap(data_bitmap, 0);

	// update inode for root directory
	struct inode *root_inode = (struct inode *)malloc(sizeof(struct inode));
	memset(root_inode, 0, sizeof(struct inode));
	root_inode->vstat.st_ino = 0;
	root_inode->ino = 0;
	root_inode->valid = 1;
	root_inode->size = 0;
	root_inode->type = 1;
	root_inode->link = 1;
	root_inode->vstat.st_mode = S_IFDIR | 0755;
	root_inode->vstat.st_nlink = 2;
	root_inode->vstat.st_gid = getgid();
	root_inode->vstat.st_uid = getuid();
	time(&root_inode->vstat.st_mtime);

	// write root inode to disk
	writei(0, root_inode);

	// write superblock to disk
	bio_write(0, sb_block);

	// write inode bitmap to disk
	bio_write(1, inode_bitmap_block);

	// write data block bitmap to disk
	bio_write(2, data_bitmap_block);

	return 0;
}


/* 
 * FUSE file operations
 */
static void *rufs_init(struct fuse_conn_info *conn) {

	// create a log file in the current directory
	log_file = fopen("/home/ypp10/OS-p4/log.txt", "w+");
	fclose(log_file);
	log_file = fopen("/home/ypp10/OS-p4/log.txt", "a");
	if (log_file == NULL) {
		perror("Error creating log file\n");
		exit(1);
	}

	 // Disable buffering for the file stream
    if (setvbuf(log_file, NULL, _IONBF, 0) != 0) {
        perror("Error disabling buffer");
        exit(1);
    }

	// write to the log file that rufs_init() is called
	fprintf(log_file, "rufs_init() is called\n");

	// Step 1a: If disk file is not found, call mkfs
	if (access(diskfile_path, F_OK) == -1) {
		rufs_mkfs();
		return NULL;
	}

  // Step 1b: If disk file is found, just initialize in-memory data structures
  // and read superblock from disk
	void* first_block = malloc(BLOCK_SIZE);
	bio_read(0, first_block);
	sb = (struct superblock *)first_block;


	// Step 2: Read inode bitmap from disk
	void* inode_bitmap_block = malloc(BLOCK_SIZE);
	bio_read(1, inode_bitmap_block);
	inode_bitmap = (bitmap_t)inode_bitmap_block;

	// Step 3: Read data block bitmap from disk
	void* data_bitmap_block = malloc(BLOCK_SIZE);
	bio_read(2, data_bitmap_block);
	data_bitmap = (bitmap_t)data_bitmap_block;

	return NULL;
}

static void rufs_destroy(void *userdata) {
	// write to the log file that rufs_destroy() is called
	fprintf(log_file, "rufs_destroy() is called\n");

	// close the log file
	fclose(log_file);

	// Step 1: De-allocate in-memory data structures

	// write all in-memory data structures to disk
	bio_write(0, (void*)sb);
	bio_write(1, (void*)inode_bitmap);
	bio_write(2, (void*)data_bitmap);

	free(sb);
	free(inode_bitmap);
	free(data_bitmap);

	// Step 2: Close diskfile
	dev_close();

}

static int rufs_getattr(const char *path, struct stat *stbuf) {
	// write to the log file that rufs_getattr() is called
	fprintf(log_file, "rufs_getattr('%s') is called\n", path);

	// Step 1: call get_node_by_path() to get inode from path
	struct inode inode;

	// if path is /, then the inode number is 0
	if (strcmp(path, "/") == 0) {
		readi(0, &inode);
		// fill attribute of root into stbuf from inode
		stbuf->st_size   = inode.size;
		stbuf->st_ino = inode.ino;
		stbuf->st_uid	= inode.vstat.st_uid;
		stbuf->st_gid	= inode.vstat.st_gid;
		stbuf->st_mode   = inode.vstat.st_mode;
		stbuf->st_nlink  = inode.vstat.st_nlink;
		stbuf->st_mtime = inode.vstat.st_mtime;
		stbuf->st_blksize = inode.vstat.st_blksize;
		stbuf->st_blocks = inode.vstat.st_blocks;
		return 0;
	}

	if (get_node_by_path(path, 0, &inode) == -1) {
		return -ENOENT;
	}

	// Step 2: fill attribute of file into stbuf from inode
	stbuf->st_size   = inode.size;
	stbuf->st_ino	= inode.ino;
	stbuf->st_uid	= inode.vstat.st_uid;
	stbuf->st_gid	= inode.vstat.st_gid;
	stbuf->st_mode   = inode.vstat.st_mode;
	stbuf->st_nlink  = inode.vstat.st_nlink;
	stbuf->st_mtime = inode.vstat.st_mtime;
	stbuf->st_blksize = inode.vstat.st_blksize;
	stbuf->st_blocks = inode.vstat.st_blocks;


	return 0;
}

static int rufs_opendir(const char *path, struct fuse_file_info *fi) {
	// write to the log file that rufs_opendir()) is called
	fprintf(log_file, "rufs_opendir() is called\n");

	// Step 1: Call get_node_by_path() to get inode from path
	struct inode inode;
	if (get_node_by_path(path, 0, &inode) == -1) {
		// Step 2: If not find, return -1
		return -1;
	}

	// if path is to a file, return -1
	if (inode.type == 0) {
		return -1;
	}

	// set fi->fh as inode number
	fi->fh = inode.ino;

    return 0;
}

static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
	// write to the log file that rufs_readdir() is called
	fprintf(log_file, "rufs_readdir() is called\n");

	// Step 1: Call get_node_by_path() to get inode from path
	struct inode inode;
	if (get_node_by_path(path, 0, &inode) == -1) {
		// Step 2: If not find, return -1
		return -1;
	}

	// check if the inode type is a file
	if (inode.type == 0) {
		return -1;
	}

	// Step 2: Read directory entries from its data blocks, and copy them to filler
	size_t num_of_entries = inode.size / sizeof(struct dirent);

	size_t num_of_data_blocks = num_of_entries / (BLOCK_SIZE / sizeof(struct dirent));

	size_t num_of_entries_in_last_block = num_of_entries % (BLOCK_SIZE / sizeof(struct dirent));

	if (num_of_entries == 0) {
		return 0;
	}

	if (num_of_entries_in_last_block == 0) {
		num_of_data_blocks -= 1;
		num_of_entries_in_last_block = BLOCK_SIZE / sizeof(struct dirent);
	}

	void* data_block = malloc(BLOCK_SIZE);
	for (size_t i = 0; i < num_of_data_blocks + 1; i++) {
		bio_read(sb->d_start_blk + inode.direct_ptr[i], data_block);

		if (i == num_of_data_blocks) {
			for (size_t j = 0; j < num_of_entries_in_last_block; j++) {
				struct dirent* entry = (struct dirent*)(data_block + j * sizeof(struct dirent));
				struct inode entry_i;
				readi(entry->ino, &entry_i);
				filler(buffer, entry->name, &entry_i.vstat, 0);
			}
		} else {
			for (size_t j = 0; j < BLOCK_SIZE / sizeof(struct dirent); j++) {
				struct dirent* entry = (struct dirent*)(data_block + j * sizeof(struct dirent));
				struct inode entry_i;
				readi(entry->ino, &entry_i);
				filler(buffer, entry->name, &entry_i.vstat, 0);
			}
		}
	}

	free(data_block);
	return 0;
}


static int rufs_mkdir(const char *path, mode_t mode) {
	// write to the log file that rufs_mkdir() is called
	fprintf(log_file, "rufs_mkdir('%s') is called\n", path);
	
	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	
	
	size_t num_of_tokens = 0;

	// tokenize the path
	char** tokens = tokenize(path, "/", &num_of_tokens);

	uint16_t current_root_ino = 0;

	struct inode inode;

	readi(current_root_ino, &inode);

	for (size_t i = 0; i < num_of_tokens; i++) {
		char* target_name = tokens[i];
		fprintf(log_file, "target_name: '%s'\n", target_name);
		// Step 2: Call get_node_by_path() to get inode of parent directory
		if (get_node_by_path(target_name, current_root_ino, &inode) == 0) {
			current_root_ino = inode.ino;
			fprintf(log_file, "target_name: '%s'\n", target_name);
			continue;
		}

		// Step 3: Call get_avail_ino() to get an available inode number
		uint16_t ino = get_avail_ino();

		// Step 4: Call dir_add() to add directory entry of target directory to parent directory
		dir_add(inode, ino, target_name, strlen(target_name));

		// Step 5: Update inode for target directory
		struct inode *target_inode = (struct inode *)malloc(sizeof(struct inode));
		memset(target_inode, 0, sizeof(struct inode));
	
		target_inode->vstat.st_ino = ino;
		target_inode->ino = ino;
		target_inode->valid = 1;
		target_inode->size = 0;
		target_inode->type = 1;
		target_inode->link = 1;
		target_inode->vstat.st_mode = S_IFDIR | mode;
		target_inode->vstat.st_nlink = 2;
		target_inode->vstat.st_gid = getgid();
		target_inode->vstat.st_uid = getuid();
		time(&target_inode->vstat.st_mtime);

		// Step 6: Call writei() to write inode to disk
		writei(ino, target_inode);

		// update the current root inode
		current_root_ino = ino;

		// update the inode for the current root directory
		inode = *target_inode;

		// free the target_inode
		free(target_inode);

	}

	// free the tokens
	free_tokens(tokens, num_of_tokens);

	fprintf(log_file, "rufs_mkdir() returns 0\n");
	return 0;
}

static int rufs_rmdir(const char *path) {

	// OPTIONAL

	return 0;
}

static int rufs_releasedir(const char *path, struct fuse_file_info *fi) {
	
	// OPTIONAL

    return 0;
}

static int rufs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
	// write to the log file that rufs_create() is called
	fprintf(log_file, "rufs_create('%s') is called\n", path);

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	char* path_copy_1 = strdup(path);
	char* path_copy_2 = strdup(path);
	char* parent_path = dirname(path_copy_1);
	char* target_name = basename(path_copy_2);

	// Step 2: Call get_node_by_path() to get inode of parent directory
	struct inode parent_inode;
	if (get_node_by_path(parent_path, 0, &parent_inode) == -1) {
		return -1;
	}

	// Step 3: Call get_avail_ino() to get an available inode number
	uint16_t ino = get_avail_ino();

	// Step 4: Call dir_add() to add directory entry of target file to parent directory
	dir_add(parent_inode, ino, target_name, strlen(target_name));

	// Step 5: Update inode for target file
	struct inode *target_inode = (struct inode *)malloc(sizeof(struct inode));
	memset(target_inode, 0, sizeof(struct inode));
	target_inode->vstat.st_ino = ino;
	target_inode->ino = ino;
	target_inode->valid = 1;
	target_inode->size = 0;
	target_inode->type = 0;
	target_inode->link = 1;
	target_inode->vstat.st_mode = S_IFREG | mode;
	target_inode->vstat.st_nlink = 2;
	target_inode->vstat.st_gid = getgid();
	target_inode->vstat.st_uid = getuid();
	time(&target_inode->vstat.st_mtime);

	// Step 6: Call writei() to write inode to disk
	writei(ino, target_inode);

	// set fi->fh as inode number
	fi->fh = ino;

	return 0;
}

static int rufs_open(const char *path, struct fuse_file_info *fi) {
	// write to the log file that rufs_open() is called
	fprintf(log_file, "rufs_open() is called\n");

	// Step 1: Call get_node_by_path() to get inode from path
	struct inode inode;
	if (get_node_by_path(path, 0, &inode) == -1) {
		// Step 2: If not find, return -1
		return -1;
	}

	// set fi->fh as inode number
	fi->fh = inode.ino;

	return 0;
}

static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// write to the log file that rufs_read() is called
	fprintf(log_file, "rufs_read() is called\n");

	// Step 1: You could call get_node_by_path() to get inode from path
	struct inode inode;
	if (get_node_by_path(path, 0, &inode) == -1) {
		return 0;
	}

	// check if the inode type is a file
	if (inode.type == 1) {
		return 0;
	}

	if (offset >= inode.size) {
		return 0;
	}

	size = inode.size > (offset + size) ? size : inode.size - offset;

	// Step 2: Based on size and offset, read its data blocks from disk
	size_t start_data_block = offset / BLOCK_SIZE;

	size_t block_offset = offset % BLOCK_SIZE;

	size_t end_data_block = (offset + size) / BLOCK_SIZE;

	void* data_block = malloc(BLOCK_SIZE);

	size_t end_data_block_offset = (offset + size) % BLOCK_SIZE;

	for (size_t i = start_data_block; i <= end_data_block; i++) {
		bio_read(sb->d_start_blk + inode.direct_ptr[i], data_block);

		if (i == start_data_block) {
			memcpy(buffer, data_block + block_offset, BLOCK_SIZE - block_offset);
		} else if (i == end_data_block) {
			memcpy(buffer + (i - start_data_block) * BLOCK_SIZE - block_offset, data_block, end_data_block_offset);
		} else {
			memcpy(buffer + (i - start_data_block) * BLOCK_SIZE - block_offset, data_block, BLOCK_SIZE);
		}
	}

	free(data_block);


	// Step 3: copy the correct amount of data from offset to buffer

	// Note: this function should return the amount of bytes you copied to buffer
	return size;
}

// helper function that allocates data blocks as necessary for writing
void allocate_data_blocks(struct inode* inode, size_t size, off_t offset) {
	// write to the log file that allocate_data_blocks() is called
	fprintf(log_file, "allocate_data_blocks() is called\n");
	size_t start_data_block = offset / BLOCK_SIZE;

	size_t end_data_block = (offset + size) / BLOCK_SIZE;

	for (size_t i = start_data_block; i <= end_data_block; i++) {
		if (inode->direct_ptr[i] == 0) {
			inode->direct_ptr[i] = get_avail_blkno();
		}
	}
}

static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// write to the log file that rufs_write() is called
	fprintf(log_file, "rufs_write() is called\n");

	// Step 1: You could call get_node_by_path() to get inode from path
	struct inode inode;
	if (get_node_by_path(path, 0, &inode) == -1) {
		return 0; 
	}

	// check if the inode type is a file
	if (inode.type == 1) {
		return 0;
	}

	// check if offset is larger than the file size
	// this ensures there is no gap between the end of the file and the offset
	if (offset > inode.size) {
		return 0;
	}

	// check if the maximum file size is exceeded
	if (offset + size > 16 * BLOCK_SIZE) {
		size = 16 * BLOCK_SIZE - offset;
	}

	// if offset is equal to the file size, then allocate data blocks as necessary
	if (offset == inode.size) {
		allocate_data_blocks(&inode, size, offset);
	}

	// Step 3: Write the correct amount of data from offset to disk
	size_t start_data_block = offset / BLOCK_SIZE;

	size_t block_offset = offset % BLOCK_SIZE;

	size_t end_data_block = (offset + size) / BLOCK_SIZE;

	void* data_block = malloc(BLOCK_SIZE);

	size_t end_data_block_offset = (offset + size) % BLOCK_SIZE;

	for (size_t i = start_data_block; i <= end_data_block; i++) {
		bio_read(sb->d_start_blk + inode.direct_ptr[i], data_block);

		if (i == start_data_block) {
			memcpy(data_block + block_offset, buffer, BLOCK_SIZE - block_offset);
		} else if (i == end_data_block) {
			memcpy(data_block, buffer + (i - start_data_block) * BLOCK_SIZE - block_offset, end_data_block_offset);
		} else {
			memcpy(data_block, buffer + (i - start_data_block) * BLOCK_SIZE - block_offset, BLOCK_SIZE);
		}

		bio_write(sb->d_start_blk + inode.direct_ptr[i], data_block);
	}

	free(data_block);

	// Step 4: Update the inode info and write it to disk
	inode.size = inode.size > (offset + size) ? inode.size : offset + size;

	writei(inode.ino, &inode);

	// Note: this function should return the amount of bytes you write to disk
	return size;
}

static int rufs_unlink(const char *path) {

	// OPTIONAL

	return 0;
}

static int rufs_truncate(const char *path, off_t size) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_release(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int rufs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


static struct fuse_operations rufs_ope = {
	.init		= rufs_init,
	.destroy	= rufs_destroy,

	.getattr	= rufs_getattr,
	.readdir	= rufs_readdir,
	.opendir	= rufs_opendir,
	.releasedir	= rufs_releasedir,
	.mkdir		= rufs_mkdir,
	.rmdir		= rufs_rmdir,

	.create		= rufs_create,
	.open		= rufs_open,
	.read 		= rufs_read,
	.write		= rufs_write,
	.unlink		= rufs_unlink,

	.truncate   = rufs_truncate,
	.flush      = rufs_flush,
	.utimens    = rufs_utimens,
	.release	= rufs_release
};


int main(int argc, char *argv[]) {
	int fuse_stat;

	getcwd(diskfile_path, PATH_MAX);
	strcat(diskfile_path, "/DISKFILE");

	fuse_stat = fuse_main(argc, argv, &rufs_ope, NULL);

	return fuse_stat;
}

