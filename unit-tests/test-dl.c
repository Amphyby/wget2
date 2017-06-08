/*
 * Copyright(c) 2017 Free Software Foundation, Inc.
 *
 * This file is part of Wget.
 *
 * Wget is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Wget is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Wget.  If not, see <https://www.gnu.org/licenses/>.
 *
 *
 * Dynamic loading related testing
 *
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>

#include <wget.h>

#include "../src/wget_dl.h"

#define abortmsg(...) \
do { \
	printf(__FILE__ ":%d: error: ", __LINE__); \
	printf(__VA_ARGS__); \
	printf("\n"); \
	abort(); \
} while (0)

#define libassert(expr) \
do { \
	if (! (expr)) { \
		abortmsg("Failed assertion [" #expr "]: %s", \
				strerror(errno)); \
	} \
} while(0)

#define OBJECT_DIR ".test_dl_dir"

#if defined _WIN32
#define BUILD_NAME(x) ".libs" "/lib" x ".dll"
#define LOCAL_NAME(x) OBJECT_DIR "/lib" x ".dll"
#else
#define BUILD_NAME(x) ".libs" "/lib" x ".so"
#define LOCAL_NAME(x) OBJECT_DIR "/lib" x ".so"
#endif

static void dump_list(char **list, size_t list_len)
{
	size_t i;

	for (i = 0; i < list_len; i++)
		printf("  %s\n", list[i]);
}

static void free_list(char **list, size_t list_len)
{
	size_t i;

	for (i = 0; i < list_len; i++)
		wget_free(list[i]);

	wget_free(list);
}

static int remove_rpl(const char *filename)
{
	int res;

	res = remove(filename);
	if (res < 0)
		if (errno == EACCES)
			res = rmdir(filename);

	return res;
}

static void remove_object_dir(void)
{
	DIR *dirp;
	struct dirent *ent;

	dirp = opendir(OBJECT_DIR);
	if (! dirp)
		return;

	while((ent = readdir(dirp)) != NULL) {
		if (strcmp(ent->d_name, ".") == 0
				|| strcmp(ent->d_name, "..") == 0)
			continue;
		char *filename = wget_aprintf(OBJECT_DIR "/%s", ent->d_name);
		libassert(remove_rpl(filename) == 0);
		wget_free(filename);
	}

	closedir(dirp);

	remove_rpl(OBJECT_DIR);
}

static void copy_file(const char *src, const char *dst)
{
	struct stat statbuf;
	int sfd, dfd;
	char buf[256];
	size_t size_remain;

	printf("  Copying %s --> %s\n", src, dst);

	libassert(stat(src, &statbuf) == 0);
	libassert((sfd = open(src, O_RDONLY | O_BINARY)) >= 0);
	libassert((dfd = open(dst, O_WRONLY | O_CREAT | O_BINARY,
					statbuf.st_mode)) >= 0);
	size_remain = statbuf.st_size;
	while(size_remain > 0) {
		ssize_t io_size = size_remain;
		if (io_size > (ssize_t) sizeof(buf))
			io_size = sizeof(buf);
		libassert(read(sfd, buf, io_size) == io_size);
		libassert(write(dfd, buf, io_size) == io_size);
		size_remain -= io_size;
	}
	close(sfd);
	close(dfd);
}

static void add_empty_file(const char *filename)
{
	char *rpl_filename = wget_aprintf(OBJECT_DIR "/%s", filename);
	FILE *stream;
	printf("  Adding file %s\n", rpl_filename);
	libassert(stream = fopen(rpl_filename, "w"));
	fclose(stream);
	wget_free(rpl_filename);
}

#define dl_assert(stmt) \
do { \
	dl_error_t e[1]; \
	dl_error_init(e); \
	stmt; \
	if (dl_error_is_set(e)) {\
		abortmsg("Failed dynamic loading operation " \
				"[" #stmt "]: %s", dl_error_get_msg(e)); \
	} \
} while(0)

typedef void (*test_fn)(char buf[16]);
static void test_fn_check(void *fn, const char *expected)
{
	char buf[16];
	test_fn fn_p;
	*((void **) &fn_p) = fn;
	(*fn_p)(buf);
	if (strncmp(buf, expected, 15) != 0)
	{
		abortmsg("Test function returned %s, expected %s",
				buf, expected);
	}
}

//Test whether dl_list1() works
static void test_dl_list(void)
{
	char **names;
	size_t names_len;
	int fail = 0;

	remove_object_dir();
	libassert(mkdir(OBJECT_DIR, 0755) == 0);
	copy_file(BUILD_NAME("alpha"), LOCAL_NAME("alpha"));
	copy_file(BUILD_NAME("beta"), LOCAL_NAME("beta"));
	add_empty_file("x");
	add_empty_file("file_which_is_not_a_library");
	add_empty_file("libreoffice.png");
	add_empty_file("not_a_library.so");
	add_empty_file("not_a_library.dylib");
	add_empty_file("not_a_library.bundle");
	libassert(mkdir(OBJECT_DIR "/somedir", 0755) == 0);
	libassert(mkdir(OBJECT_DIR "/libactuallyadir.so", 0755) == 0);
	libassert(mkdir(OBJECT_DIR "/libactuallyadir.dll", 0755) == 0);
	libassert(mkdir(OBJECT_DIR "/libactuallyadir.dylib", 0755) == 0);
	libassert(mkdir(OBJECT_DIR "/libactuallyadir.bundle", 0755) == 0);

	libassert(dl_list1(OBJECT_DIR, &names, &names_len) == 0);

	if (names_len != 2) {
		fail = 1;
	} else {
		if (strcmp(names[0], "alpha") == 0) {
			if (strcmp(names[1], "beta") != 0)
				fail = 1;
		} else if (strcmp(names[0], "beta") == 0) {
			if (strcmp(names[1], "alpha") != 0)
				fail = 1;
		} else {
			fail = 1;
		}
	}
	if (fail == 1) {
		printf("dl_list1() returned incorrect list\n");
		printf("List contains\n");
		dump_list(names, names_len);
		abort();
	}

	free_list(names, names_len);
}


//Test whether symbols from dynamically loaded libraries link as expected
static void test_linkage(void)
{
	dl_file_t *dm_alpha, *dm_beta;
	void *fn;

	//Create test directory
	remove_object_dir();
	libassert(mkdir(OBJECT_DIR, 0755) == 0);
	copy_file(BUILD_NAME("alpha"), LOCAL_NAME("alpha"));
	copy_file(BUILD_NAME("beta"), LOCAL_NAME("beta"));

	//Load both libraries
	dl_assert(dm_alpha = dl_file_open(LOCAL_NAME("alpha"), e));
	dl_assert(dm_beta = dl_file_open(LOCAL_NAME("beta"), e));

	//Check whether symbols load
	dl_assert(fn = dl_file_lookup(dm_alpha, "dl_test_fn_alpha", e));
	test_fn_check(fn, "alpha");
	dl_assert(fn = dl_file_lookup(dm_beta, "dl_test_fn_beta", e));
	test_fn_check(fn, "beta");

	//Check behavior in case of nonexistent symbol
	{
		dl_error_t e[1];

		dl_error_init(e);

		fn = dl_file_lookup(dm_alpha, "dl_test_fn_beta", e);
		if (fn || (! dl_error_is_set(e)))
			abortmsg("nonexistent symbols not returning error");

		dl_error_set(e, NULL);
	}

	//Check behavior in case of multiple libraries exporting
	//symbols with same name
	dl_assert(fn = dl_file_lookup(dm_alpha, "dl_test_write_param", e));
	test_fn_check(fn, "alpha");
	dl_assert(fn = dl_file_lookup(dm_beta, "dl_test_write_param", e));
	test_fn_check(fn, "beta");

	dl_file_close(dm_alpha);
	dl_file_close(dm_beta);
}

#define run_test(test) \
do { \
	printf("Running " #test "...\n"); \
	test(); \
	printf("PASS " #test "\n"); \
} while (0)


int main(G_GNUC_WGET_UNUSED int argc, char **argv)
{
	if (! dl_supported()) {
		printf("Skipping dynamic loading tests\n");

		return 77;
	}

	// if VALGRIND testing is enabled, we have to call ourselves with
	// valgrind checking
	const char *valgrind = getenv("VALGRIND_TESTS");

	if (!valgrind || !*valgrind || !strcmp(valgrind, "0")) {
		// fallthrough
	}
	else if (!strcmp(valgrind, "1")) {
		char cmd[strlen(argv[0]) + 256];

		snprintf(cmd, sizeof(cmd), "VALGRIND_TESTS=\"\" valgrind "
				"--error-exitcode=301 --leak-check=yes "
				"--show-reachable=yes --track-origins=yes %s",
				argv[0]);
		return system(cmd) != 0;
	} else {
		char cmd[strlen(valgrind) + strlen(argv[0]) + 32];

		snprintf(cmd, sizeof(cmd), "VALGRIND_TESTS="" %s %s",
				valgrind, argv[0]);
		return system(cmd) != 0;
	}

	run_test(test_dl_list);
	run_test(test_linkage);

	remove_object_dir();

	return 0;
}