#ifndef LIBSDB_LIBSDB_FILENO_H
#define LIBSDB_LIBSDB_FILENO_H

#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>

//#define LIBSDB_DEBUG

#ifdef LIBSDB_DEBUG
	#include <stdio.h>
#endif

char st_buffer[LIBSDB_MAXVALUE];

inline static bool prepare_path(const char *dir, const char *file, char *buffer) {
	size_t dir_len = strlen(dir);
	size_t file_len = strlen(file);

	if (file_len > NAME_MAX or file_len + dir_len + 1 > PATH_MAX) return false;

	char *fly = memcpy(buffer, dir, dir_len) + dir_len;
	*fly = '/';
	fly++;
	memcpy(fly, file, file_len + 1);

	return true;
}

bool sdb_insert_fileno(sdb_dbo *db, const char *key, const char *value) {
	char path_buffer[PATH_MAX] = "";
	prepare_path(db->dataset, key, path_buffer);

#ifdef LIBSDB_DEBUG
	fprintf(stderr, "Requested INSERT to path: %s key: %s value: %s\n", path_buffer, key, value);
#endif

	int fd = open(path_buffer, O_WRONLY | O_CREAT | O_EXCL, 0666);

#ifdef LIBSDB_DEBUG
	fprintf(stderr, "Opening file. fd: %d errno: %d\n", fd, errno);
#endif

	if (fd < 0) {
		if (errno == ENOMEM) enomem_flag = 1;
		return false;
	}
	errno = 0;

	size_t value_length = strlen(value);
	ssize_t got = write(fd, value, value_length);

#ifdef LIBSDB_DEBUG
	fprintf(stderr, "Writed contents to file. requested: %zu got: %zd\n", value_length, got);
#endif

	if (got < 0) {
		if (errno == ENOMEM or errno == EFBIG or errno == ENOSPC) enomem_flag = 1;
		close(fd);
		return false;
	}

	// You may wondering why "not enough space" checking is so shitty.
	// That's because in practice filesystem may write less amount of bytes then requested without setting errno. Whoa!
	//
	// Wanna make a test?
	// Create small ext2 filesystem. Let's use 1MB size. Now create a single file and try to write 1.5MB data.
	// write() will return something around 1MB. errno is still 0! What?!
	// Ok, now more - TRY TO CALL write() WITH SUPER SMALL AMOUNT OF BYTES: ssize_t got = write(fd, "aaa", 3);
	// And that call will return... 3. Well fucking done, Linux! Well fucking done.

	// TODO: research O_DIRECT

	if (errno == ENOMEM or errno == EFBIG or errno == ENOSPC or (size_t) got < value_length) enomem_flag = 1;
	close(fd);

	return true;
}

bool sdb_update_fileno(sdb_dbo *db, const char *key, const char *value) {
	char path_buffer[PATH_MAX] = "";
	prepare_path(db->dataset, key, path_buffer);

#ifdef LIBSDB_DEBUG
	fprintf(stderr, "Requested UPDATE to path: %s key: %s value: %s\n", path_buffer, key, value);
#endif

	int fd = open(path_buffer, O_WRONLY | O_TRUNC);

#ifdef LIBSDB_DEBUG
	fprintf(stderr, "Opening and truncating file. fd: %d errno: %d\n", fd, errno);
#endif

	if (fd < 0) {
		if (errno == ENOMEM) enomem_flag = 1;
		return false;
	}
	errno = 0;

	size_t value_length = strlen(value);
	ssize_t got = write(fd, value, value_length);

#ifdef LIBSDB_DEBUG
	fprintf(stderr, "Writed contents to file. requested: %zu got: %zd\n", value_length, got);
#endif

	if (got < 0) {
		if (errno == ENOMEM or errno == EFBIG or errno == ENOSPC) enomem_flag = 1;
		close(fd);
		return false;
	}
	if (errno == ENOMEM or errno == EFBIG or errno == ENOSPC or (size_t) got < value_length) enomem_flag = 1;
	close(fd);

	return true;
}

const char *sdb_select_fileno(sdb_dbo *db, const char *key) {
	char path_buffer[PATH_MAX] = "";
	prepare_path(db->dataset, key, path_buffer);

	int fd = open(path_buffer, O_RDONLY);
	if (fd < 0) {
		if (errno == ENOMEM) enomem_flag = 1;
		return NULL;
	}
	errno = 0;

	char *readbuffer;
	size_t readsize;

	if (your_own_buffer != NULL and your_own_buffer_size > 0) {
		readbuffer = your_own_buffer;
		readsize = your_own_buffer_size;
	} else {
		readbuffer = st_buffer;
		readsize = LIBSDB_MAXVALUE - 1;
	}
	ssize_t got = read(fd, readbuffer, readsize);

	if (got < 0) {
		close(fd);
		return false;
	}
	if (errno == ENOMEM or errno == EFBIG or errno == ENOSPC) enomem_flag = 1;
	close(fd);

	readbuffer[got] = '\0';
	read_size_hook = got;

	return st_buffer;
}

bool sdb_delete_fileno(sdb_dbo *db, const char *key) {
	char path_buffer[PATH_MAX] = "";
	prepare_path(db->dataset, key, path_buffer);

	if (unlink(path_buffer) < 0) return false;
	return true;
}

ssize_t sdb_exist_fileno(sdb_dbo *db, const char *key) {
	char path_buffer[PATH_MAX] = "";
	prepare_path(db->dataset, key, path_buffer);

	if (access(path_buffer, R_OK | W_OK) < 0) return false;
	struct stat st;
	if (stat(db->dataset, &st) < 0) return false;
	return st.st_size;
}

sdb_dbo *sdb_open_fileno(sdb_engine engine, void *params) {
	if (engine != SDB_FILENO) return false;

	sdb_dbo *source = my_calloc(sizeof(sdb_dbo), sizeof(char));
	if (source == NULL) return NULL;

	if (params != NULL) source->dataset = params; else
		source->dataset = "sdb_storage";

	if (access(source->dataset, W_OK) == 0) {
		source->defun.p_sdb_insert = sdb_insert_fileno;
		source->defun.p_sdb_update = sdb_update_fileno;
		source->defun.p_sdb_select = sdb_select_fileno;
		source->defun.p_sdb_delete = sdb_delete_fileno;
		source->defun.p_sdb_exist = sdb_exist_fileno;
		return source;
	} else {
		my_free(source);
		return NULL;
	}
}

#endif //LIBSDB_LIBSDB_FILENO_H
