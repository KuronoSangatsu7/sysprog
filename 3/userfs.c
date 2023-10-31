#include "userfs.h"
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include<stdio.h>

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
create_file(const char *filename)
{
	struct file * new_file = malloc(sizeof(struct file));
	new_file->name = malloc(strlen(filename) + 1);
	strcpy(new_file->name, filename);
	new_file->refs = 0;

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
	if (fd_id < 0 || fd_id >= file_descriptor_capacity) {
		return -1;
	}

	if (file_descriptors[fd_id] == NULL) {
		return -1;
	}

	file_descriptors[fd_id]->file->refs --;

	free(file_descriptors[fd_id]);
	file_descriptors[fd_id] = NULL;
	file_descriptor_count --;
	return 0;
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

	free(file->name);
	free(file);
	return 0;
}

int
ufs_open(const char *filename, int flags)
{
	struct file * current_file = file_list;
	
	while(current_file != NULL) {
		if (strcmp(current_file->name, filename) == 0) {
			break;
		}
		current_file = current_file->next;
	}
	
	bool CREATE = flags & UFS_CREATE;

	if (!CREATE && current_file == NULL) {
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}
	
	if (current_file == NULL && CREATE)
		current_file = create_file(filename);
	
	struct filedesc* fd = malloc(sizeof(struct filedesc));

	fd->file = current_file;
	current_file->refs ++;

	int fd_id = add_descriptor(fd);
	printf("file descriptor count: %d\n", file_descriptor_count);
	return fd_id;
}

ssize_t
ufs_write(int fd, const char *buf, size_t size)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)fd;
	(void)buf;
	(void)size;
	ufs_error_code = UFS_ERR_NOT_IMPLEMENTED;
	return -1;
}

ssize_t
ufs_read(int fd, char *buf, size_t size)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)fd;
	(void)buf;
	(void)size;
	ufs_error_code = UFS_ERR_NOT_IMPLEMENTED;
	return -1;
}

int
ufs_close(int fd)
{
	return remove_descriptor(fd);
}

int
ufs_delete(const char *filename)
{
	struct file * current_file = file_list;
	
	while(current_file != NULL) {
		if (strcmp(current_file->name, filename) == 0) {
			break;
		}
		current_file = current_file->next;
	}

	if (current_file == NULL) {
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}

	delete_file(current_file);

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
