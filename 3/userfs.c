#include "userfs.h"
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include<stdio.h>

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

enum {
	BLOCK_SIZE = 512,
	MAX_FILE_SIZE = 1024 * 1024 * 100,
};

/** Global error code. Set from any function on any error. */
static enum ufs_error_code ufs_error_code = UFS_ERR_NO_ERR;

struct block {
	/** Block memory. */
	char *memory;
	/** How many bytes are occupied. */
	int occupied;
	/** Next block in the file. */
	struct block *next;
	/** Previous block in the file. */
	struct block *prev;

	/* PUT HERE OTHER MEMBERS */
	int index;
};

struct file {
	/** Double-linked list of file blocks. */
	struct block *block_list;
	/**
	 * Last block in the list above for fast access to the end
	 * of file.
	 */
	struct block *last_block;
	/** How many file descriptors are opened on the file. */
	int refs;
	/** File name. */
	char *name;
	/** Files are stored in a double-linked list. */
	struct file *next;
	struct file *prev;

	/* PUT HERE OTHER MEMBERS */
};

/** List of all files. */
static struct file *file_list = NULL;

struct filedesc {
	struct file *file;
	/* PUT HERE OTHER MEMBERS */

	struct block *current_block;
	int id;
	int block_offset;
	int flags;
};

/**
 * An array of file descriptors. When a file descriptor is
 * created, its pointer drops here. When a file descriptor is
 * closed, its place in this array is set to NULL and can be
 * taken by next ufs_open() call.
 */
static struct filedesc **file_descriptors = NULL;
static int file_descriptor_count = 0;
static int file_descriptor_capacity = 0;

enum ufs_error_code
ufs_errno()
{
	return ufs_error_code;
}

struct file*
get_file(const char *filename)
{
	struct file * current_file = file_list;
	
	while(current_file != NULL) {
		if (strcmp(current_file->name, filename) == 0) {
			break;
		}
		current_file = current_file->next;
	}

	return current_file;
}

struct filedesc*
get_file_descriptor(int fd_id)
{
	if (fd_id < 0 || fd_id >= file_descriptor_capacity) {
		return NULL;
	}

	return file_descriptors[fd_id];
}

struct file*
create_file(const char *filename)
{
	struct file * new_file = malloc(sizeof(struct file));
	new_file->name = malloc(strlen(filename) + 1);
	strcpy(new_file->name, filename);
	new_file->refs = 0;

	struct block * new_block = malloc(sizeof(struct block));
	*new_block = (struct block) {
		.memory = malloc(BLOCK_SIZE),
		.occupied = 0,
		.next = NULL,
		.prev = NULL,
		.index = 0,
	};

	new_file->block_list = new_block;
	new_file->last_block = new_block;

	if(file_list == NULL) {
		file_list = new_file;
		new_file->next = NULL;
		new_file->prev = NULL;
	}

	else {
		struct file * current_file = file_list;
		while(current_file->next != NULL) {
			current_file = current_file->next;
		}
		current_file->next = new_file;
		new_file->prev = current_file;
		new_file->next = NULL;
	}

	return new_file;
}

int
delete_file(struct file *file)
{
	if (file == NULL) {
		return -1;
	}

	if (file->refs != 0) {
		return -1;
	}

	if (file->next == NULL && file->prev == NULL) {
		file_list = NULL;
	}

	else if (file->next == NULL) {
		file->prev->next = NULL;
	}

	else if (file->prev == NULL) {
		file->next->prev = NULL;
		file_list = file->next;
	}

	else {
		file->prev->next = file->next;
		file->next->prev = file->prev;
	}

	struct block * current_block = file->block_list;
	while(current_block != NULL) {
		struct block * next_block = current_block->next;
		free(current_block->memory);
		free(current_block);
		current_block = next_block;
	}

	free(file->name);
	free(file);
	return 0;
}

int
add_descriptor(struct filedesc* fd)
{
	if (file_descriptor_capacity == 0) {
		file_descriptor_capacity = 10;
		file_descriptors = malloc(file_descriptor_capacity * sizeof(struct filedesc*));
	}

	if (file_descriptor_count == file_descriptor_capacity) {
		file_descriptor_capacity *= 2;
		file_descriptors = realloc(file_descriptors, file_descriptor_capacity * sizeof(struct filedesc*));
	}
	
	for (int i = 0; i < file_descriptor_capacity; i++) {
		if (file_descriptors[i] == NULL) {
			file_descriptors[i] = fd;
			file_descriptor_count ++;
			return i;
		}
	}

	return -1;	
}

int
remove_descriptor(int fd_id)
{
	struct filedesc* fd = get_file_descriptor(fd_id);

	if(fd == NULL) {
		return -1;
	}

	fd->file->refs --;

	free(fd);
	file_descriptors[fd_id] = NULL;
	file_descriptor_count --;
	return 0;
}

int
ufs_open(const char *filename, int flags)
{
	struct file * file = get_file(filename);
	
	bool CREATE = flags & UFS_CREATE;

	if (!CREATE && file == NULL) {
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}
	
	if (file == NULL && CREATE)
		file = create_file(filename);
	
	struct filedesc* fd = malloc(sizeof(struct filedesc));

	fd->file = file;
	fd->id = 0;
	fd->current_block = file->block_list;
	fd->block_offset = 0;
	fd->flags = flags;

	file->refs ++;

	int fd_id = add_descriptor(fd);
	fd -> id = fd_id;
	
	return fd_id;
}

char
read_byte(struct filedesc *fd)
{
	char byte = fd->current_block->memory[fd->block_offset];
	return byte;
}

void
write_byte(struct filedesc *fd, char byte)
{
	fd->current_block->memory[fd->block_offset] = byte;
	if (fd->file->last_block == fd->current_block
        && fd->block_offset == fd->file->last_block->occupied) {
        fd->file->last_block->occupied++;
    }
}

void
go_to_next_block(struct filedesc *fd)
{
	fd->block_offset++;
	if (fd->block_offset == BLOCK_SIZE) {
		struct block *next_block = fd->current_block->next;
		if (next_block == NULL) {
			next_block = malloc(sizeof(struct block));
			next_block->index = fd->current_block->index + 1;
			next_block->memory = malloc(BLOCK_SIZE);
			next_block->occupied = 0;
			next_block->next = NULL;
			next_block->prev = fd->current_block;
			fd->current_block->next = next_block;
			fd->file->last_block = next_block;
		}
		fd->current_block = next_block;
		fd->block_offset = 0;
	}
}

ssize_t
write_to_file(struct filedesc *fd, const char *buf, int size)
{
	int current_size = fd->current_block->index * BLOCK_SIZE + fd->block_offset;
	int new_size = current_size + size;
	if (new_size > MAX_FILE_SIZE) {
		ufs_error_code = UFS_ERR_NO_MEM;
		return -1;
	}

	for (int i = 0; i < size; i++) {
		write_byte(fd, buf[i]);
		go_to_next_block(fd);
	}

	return size;
}

ssize_t
read_from_file(struct filedesc *fd, char *buf, int size)
{
	int current_size = fd->current_block->index * BLOCK_SIZE + fd->block_offset;
	int file_size = fd->file->last_block->index * BLOCK_SIZE + fd->file->last_block->occupied;
	int bytes_to_read = file_size - current_size;
	int bytes_read = MIN(bytes_to_read, size);
	
	for (int i = 0; i < bytes_read; i++) {
		buf[i] = read_byte(fd);
		go_to_next_block(fd);
	}
	return bytes_read;
}

ssize_t
ufs_write(int fd, const char *buf, size_t size)
{
	struct filedesc * file_descriptor = get_file_descriptor(fd);
	if (file_descriptor == NULL) {
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}

	if (file_descriptor->flags & UFS_READ_ONLY) {
		ufs_error_code = UFS_ERR_NO_PERMISSION;
		return -1;
	}

	return write_to_file(file_descriptor, buf, size);
}

ssize_t
ufs_read(int fd, char *buf, size_t size)
{
	struct filedesc * file_descriptor = get_file_descriptor(fd);
	if (file_descriptor == NULL) {
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}
	
	if(file_descriptor->flags & UFS_WRITE_ONLY) {
		ufs_error_code = UFS_ERR_NO_PERMISSION;
		return -1;
	}
	
	return read_from_file(file_descriptor, buf, size);
}

int
ufs_close(int fd)
{
	return remove_descriptor(fd);
}

int
ufs_delete(const char *filename)
{
	struct file * file = get_file(filename);

	if (file == NULL) {
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}

	delete_file(file);

	return 0;
}

void
ufs_destroy(void)
{
	struct filedesc **fd = file_descriptors;
	for (int i = 0; i < file_descriptor_capacity; i++) {
		if (fd != NULL) {
			remove_descriptor(i);
		}
		fd ++;
	}
	free(file_descriptors);
	
	struct file * current_file = file_list;
	while(current_file != NULL) {
		delete_file(current_file);
		current_file = file_list;
	}
}
