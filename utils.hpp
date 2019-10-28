// #include <alloca.h>
#include <unistd.h>


typedef enum __err {
	ERR_NONE = 0,
	ERR_WRONG_PARAMS,
	ERR_NOTA_DIR,
	ERR_NEXISTS_DIR,
	ERR_EXISTS_DIR,
	ERR_NOTA_FILE,
	ERR_NEXISTS_FILE,
	ERR_EXISTS_FILE,
	ERR_CANT_CREATE_DIR,
	ERR_CANT_DELETE_DIR,
	ERR_CANT_DELETE_FILE,
	ERR_CANT_OPEN_FD,
	ERR_CANT_CLOSE_FD,
	ERR_CANT_WRITE_FD,
	ERR_CANT_READ_FD,
	ERR_CANT_WALK_FD,
	ERR_CANT_GET_SIZE_FD,
	ERR_CANT_RESIZE_FD,
	ERR_CANT_CREATE_PIPE,
	ERR_CANT_SPAWN_PROC,
	ERR_CANT_WAIT_PROC
} err_t;


// INLINE
// const char *rel_to_abs_path(const char *path, size_t *length) {
// 		app.traces_folder_path_length = 0;
// 		app.traces_folder_path_length += strlen(config.traces_dir);

// 		app.traces_folder_path = (char *)malloc(app.traces_folder_path_length + 1);
// 		snprintf(app.traces_folder_path, app.traces_folder_path_length + 1,
// 			"%s", config.traces_dir);
// }


INLINE
bool utils_str_equal(const char *str1, const char *str2) {
	return (strcmp(str1, str2) == 0);
}


const char *utils_str_concat(size_t count, ...) {
	size_t length, pos;
	const char *str;
	char *copy;

	va_list ap;
	unsigned int i;

	va_start(ap, count);
	for (i = 0, length = 1; i < count; ++i, length += strlen(str)) {
		str = va_arg(ap, char *);
	}
	va_end(ap);

	copy = (char *)malloc(length);
	if (copy == NULL) {
		return NULL;
	}

	va_start(ap, count);
	for (i = 0, pos = 0; i < count; ++i, pos += length) {
		str = va_arg(ap, char *);
		length = strlen(str);
		memcpy(copy + pos, str, length);
	}
	va_end(ap);

	return copy;
}


INLINE
err_t utils_check_dir(const char *path) {
	OS_FILE_ATTRIBUTES attrs;
	OS_GetFileAttributes(path, &attrs);

	if ((attrs & OS_FILE_ATTRIBUTES_EXIST) == 0) {
		return ERR_NEXISTS_DIR;
	}

	if ((attrs & OS_FILE_ATTRIBUTES_DIRECTORY) == 0) {
		return ERR_NOTA_DIR;
	}

	return ERR_NONE;
}


INLINE
err_t utils_check_file(const char *path) {
	OS_FILE_ATTRIBUTES attrs;
	OS_GetFileAttributes(path, &attrs);

	if ((attrs & OS_FILE_ATTRIBUTES_EXIST) == 0) {
		return ERR_NEXISTS_FILE;
	}

	if ((attrs & OS_FILE_ATTRIBUTES_DIRECTORY) != 0) {
		return ERR_NOTA_FILE;
	}

	return ERR_NONE;
}


INLINE
err_t utils_create_dir(const char *dirname) {
	OS_RETURN_CODE ret;

	ret = OS_MkDir(dirname, 0777);

	if (ret.generic_err == OS_RETURN_CODE_FILE_EXIST) {
		return ERR_EXISTS_DIR;
	}
	else if (ret.generic_err == OS_RETURN_CODE_FILE_OPEN_FAILED) {
		return ERR_CANT_CREATE_DIR;
	}

	return ERR_NONE;
}


INLINE
err_t utils_delete_dir(const char *dirname) {
	if (OS_DeleteDirectory(dirname)
		.generic_err == OS_RETURN_CODE_FILE_DELETE_FAILED) {
		return ERR_CANT_DELETE_DIR;
	}

	return ERR_NONE;
}


INLINE
err_t utils_delete_file(const char *filename) {
	if (OS_DeleteFile(filename)
		.generic_err == OS_RETURN_CODE_FILE_DELETE_FAILED) {
		return ERR_CANT_DELETE_FILE;
	}

	return ERR_NONE;
}


INLINE
err_t utils_open_fd(const char *pathname, const char *mode, unsigned long *fd) {
	unsigned int open_type;
	unsigned int perm_type;

	if (utils_str_equal(mode, "r")) {
		// Open text file for reading.  The stream is positioned at the
		// beginning of the file.
		open_type = OS_FILE_OPEN_TYPE_READ;
		perm_type = OS_FILE_PERMISSION_TYPE_READ;
	} else
	if (utils_str_equal(mode, "r+")) {
		// Open for reading and writing.  The stream is positioned at the
		// beginning of the file.
		open_type = OS_FILE_OPEN_TYPE_READ | OS_FILE_OPEN_TYPE_WRITE;
		perm_type = OS_FILE_PERMISSION_TYPE_READ | OS_FILE_PERMISSION_TYPE_WRITE;
	} else
	if (utils_str_equal(mode, "w")) {
		// Truncate file to zero length or create text file for writing.
		// The stream is positioned at the beginning of the file.
		open_type = OS_FILE_OPEN_TYPE_CREATE | OS_FILE_OPEN_TYPE_TRUNCATE | OS_FILE_OPEN_TYPE_WRITE;
		perm_type = OS_FILE_PERMISSION_TYPE_WRITE;
	} else
	if (utils_str_equal(mode, "w+")) {
		// Open for reading and writing.  The file is created if it does
		// not exist, otherwise it is truncated.  The stream is
		// positioned at the beginning of the file.
		open_type = OS_FILE_OPEN_TYPE_CREATE | OS_FILE_OPEN_TYPE_TRUNCATE
		            | OS_FILE_OPEN_TYPE_READ | OS_FILE_OPEN_TYPE_WRITE;
		perm_type = OS_FILE_PERMISSION_TYPE_READ | OS_FILE_PERMISSION_TYPE_WRITE;
	} else
	if (utils_str_equal(mode, "a")) {
		// Open for appending (writing at end of file).  The file is
		// created if it does not exist.  The stream is positioned at the
		// end of the file.
		open_type = OS_FILE_OPEN_TYPE_CREATE | OS_FILE_OPEN_TYPE_APPEND | OS_FILE_OPEN_TYPE_WRITE;
		perm_type = OS_FILE_PERMISSION_TYPE_WRITE;
	} else
	if (utils_str_equal(mode, "a+")) {
		// Open for reading and appending (writing at end of file). The
		// file is created if it does not exist.  Output is always
		// appended to the end of the file.  POSIX is silent on what the
		// initial read position is when using this mode.  For glibc, the
		// initial file position for reading is at the beginning of the
		// file, but for Android/BSD/MacOS, the initial file position for
		// reading is at the end of the file.
		open_type = OS_FILE_OPEN_TYPE_CREATE | OS_FILE_OPEN_TYPE_APPEND
		            | OS_FILE_OPEN_TYPE_READ | OS_FILE_OPEN_TYPE_WRITE;
		perm_type = OS_FILE_PERMISSION_TYPE_READ | OS_FILE_PERMISSION_TYPE_WRITE;
	} else
	{
		return ERR_WRONG_PARAMS;
	}

	if (OS_OpenFD(pathname, open_type, perm_type, fd
		).generic_err == OS_RETURN_CODE_FILE_OPEN_FAILED) {
		return ERR_CANT_OPEN_FD;
	}

	return ERR_NONE;
}


INLINE
err_t utils_close_fd(unsigned long fd) {
	if (OS_CloseFD(fd)
		.generic_err == OS_RETURN_CODE_FILE_CLOSE_FAILED) {
		return ERR_CANT_CLOSE_FD;
	}

	return ERR_NONE;
}


INLINE
err_t utils_write_fd(unsigned int fd, void *buff, size_t nbytes, size_t *writtenbytes) {
	size_t count = nbytes;

	OS_RETURN_CODE ret = OS_WriteFD(fd, buff, &count);

	*writtenbytes = count;

	if (ret.generic_err == OS_RETURN_CODE_FILE_WRITE_FAILED) {
		return ERR_CANT_WRITE_FD;
	}

	return ERR_NONE;
}


INLINE
err_t utils_read_fd(unsigned int fd, void *buff, size_t nbytes, size_t *readbytes) {
	size_t count = nbytes;

	OS_RETURN_CODE ret = OS_ReadFD(fd, &count, buff);

	*readbytes = count;

	if (ret.generic_err == OS_RETURN_CODE_FILE_READ_FAILED) {
		return ERR_CANT_READ_FD;
	}

	return ERR_NONE;
}


INLINE
err_t utils_walk_fd(unsigned int fd, const char *from, uint64_t offset, uint64_t *new_offset) {
	unsigned int whence;
	int64_t raw_offset;

	if (utils_str_equal(from, "c")) {
		whence = OS_FILE_SEEK_CUR;
	} else
	if (utils_str_equal(from, "b")) {
		whence = OS_FILE_SEEK_SET;
	} else
	if (utils_str_equal(from, "e")) {
		whence = OS_FILE_SEEK_END;
	} else
	{
		return ERR_WRONG_PARAMS;
	}

	raw_offset = offset;

	if (OS_SeekFD(fd, whence, &raw_offset)
		.generic_err == OS_RETURN_CODE_FILE_SEEK_FAILED) {
		return ERR_CANT_WALK_FD;
	}

	*new_offset = raw_offset;

	return ERR_NONE;
}


INLINE
err_t utils_get_size_fd(unsigned int fd, uint64_t *bytes) {
	if (OS_FileSizeFD(fd, bytes)
		.generic_err == OS_RETURN_CODE_FILE_QUERY_FAILED) {
		return ERR_CANT_GET_SIZE_FD;
	}

	return ERR_NONE;
}


INLINE
err_t utils_resize_fd(unsigned int fd, uint64_t bytes) {
	if (OS_Ftruncate(fd, bytes)
		.generic_err == OS_RETURN_CODE_FILE_OPERATION_FAILED) {
		return ERR_CANT_RESIZE_FD;
	}

	return ERR_NONE;
}


INLINE
err_t utils_open_pipe(unsigned long *read_fd, unsigned long *write_fd) {
	if (OS_Pipe(OS_PIPE_CREATE_FLAGS_NONE, read_fd, write_fd)
		.generic_err != OS_RETURN_CODE_NO_ERROR) {
		return ERR_CANT_CREATE_PIPE;
	}

	return ERR_NONE;
}


INLINE
unsigned long utils_get_os_page_size(void) {
	return sysconf(_SC_PAGESIZE);
}


err_t utils_spawn_process(const char *cmd, bool wait, int32_t *status, unsigned long channels[3] = NULL) {
	OS_PROCESS_WAITABLE_PROCESS process;
	UINT32 child_status;

	NATIVE_FD stdfiles[3] = { 0, 1, 2 };

	if (channels) {
		stdfiles[0] = channels[0];
		stdfiles[1] = channels[1];
		stdfiles[2] = channels[2];
	}

	if (OS_CreateProcess(cmd, stdfiles, NULL, NULL, &process)
		.generic_err == OS_RETURN_CODE_PROCESS_QUERY_FAILED) {
		return ERR_CANT_SPAWN_PROC;
	}

	if (wait && OS_WaitForProcessTermination(process, &child_status)
		.generic_err == OS_RETURN_CODE_PROCESS_QUERY_FAILED) {
		return ERR_CANT_WAIT_PROC;
	}
	else {
		*status = child_status;
	}

	return ERR_NONE;
}
