#include <stic.h>

#include <sys/time.h> /* timeval utimes() */
#include <unistd.h> /* chdir() unlink() */

#include <stddef.h> /* NULL */
#include <stdlib.h> /* free() */
#include <string.h> /* strcpy() strdup() */

#include "../../src/cfg/config.h"
#include "../../src/compat/os.h"
#include "../../src/utils/dynarray.h"
#include "../../src/utils/fs.h"
#include "../../src/utils/str.h"
#include "../../src/fops_put.h"
#include "../../src/ops.h"
#include "../../src/undo.h"

#include "utils.h"

static void perform_merge(int op);
static int file_exists(const char file[]);

static char *saved_cwd;

SETUP()
{
	saved_cwd = save_cwd();
	assert_success(chdir(SANDBOX_PATH));

	/* lwin */
	strcpy(lwin.curr_dir, ".");

	view_setup(&lwin);
	lwin.list_rows = 1;
	lwin.list_pos = 0;
	lwin.dir_entry = dynarray_cextend(NULL,
			lwin.list_rows*sizeof(*lwin.dir_entry));
	lwin.dir_entry[0].name = strdup("file");
	lwin.dir_entry[0].origin = &lwin.curr_dir[0];

	/* rwin */
	strcpy(rwin.curr_dir, ".");

	view_setup(&rwin);
	rwin.filtered = 0;
	rwin.list_pos = 0;

	curr_view = &lwin;
	other_view = &rwin;
}

TEARDOWN()
{
	view_teardown(&lwin);
	view_teardown(&rwin);

	restore_cwd(saved_cwd);
}

TEST(merge_directories)
{
#ifndef _WIN32
	replace_string(&cfg.shell, "/bin/sh");
#else
	replace_string(&cfg.shell, "cmd");
#endif

	stats_update_shell_type(cfg.shell);

	for(cfg.use_system_calls = 0; cfg.use_system_calls < 2;
			++cfg.use_system_calls)
	{
		ops_t *ops;

		create_empty_dir("first");
		create_empty_dir("first/nested");
		create_empty_file("first/nested/first-file");

		create_empty_dir("second");
		create_empty_dir("second/nested");
		create_empty_file("second/nested/second-file");

		cmd_group_begin("undo msg");

		assert_non_null(ops = ops_alloc(OP_MOVEF, 0, "merge", ".", "."));
		ops->crp = CRP_OVERWRITE_ALL;
		assert_success(merge_dirs("first", "second", ops));
		ops_free(ops);

		cmd_group_end();

		/* Original directory must be deleted. */
		assert_false(file_exists("first/nested"));
		assert_false(file_exists("first"));

		assert_true(file_exists("second/nested/second-file"));
		assert_true(file_exists("second/nested/first-file"));

		assert_success(unlink("second/nested/first-file"));
		assert_success(unlink("second/nested/second-file"));
		assert_success(rmdir("second/nested"));
		assert_success(rmdir("second"));
	}

	stats_update_shell_type("/bin/sh");
}

TEST(merge_directories_creating_intermediate_parent_dirs_move)
{
#ifndef _WIN32
	replace_string(&cfg.shell, "/bin/sh");
#else
	replace_string(&cfg.shell, "cmd");
#endif

	stats_update_shell_type(cfg.shell);

	for(cfg.use_system_calls = 0; cfg.use_system_calls < 2;
			++cfg.use_system_calls)
	{
		perform_merge(OP_MOVEF);

		/* Original directory must be deleted. */
		assert_false(file_exists("first"));
	}

	stats_update_shell_type("/bin/sh");
}

TEST(merge_directories_creating_intermediate_parent_dirs_copy)
{
#ifndef _WIN32
	replace_string(&cfg.shell, "/bin/sh");
#else
	replace_string(&cfg.shell, "cmd");
#endif

	stats_update_shell_type(cfg.shell);

	for(cfg.use_system_calls = 0; cfg.use_system_calls < 2;
			++cfg.use_system_calls)
	{
		perform_merge(OP_COPYF);

		/* Original directory must still exist. */
		assert_success(unlink("first/nested1/nested2/file"));
		assert_success(rmdir("first/nested1/nested2"));
		assert_success(rmdir("first/nested1"));
		assert_success(rmdir("first"));
	}

	stats_update_shell_type("/bin/sh");
}

TEST(error_lists_are_joined_with_newline_separator)
{
	ops_t *ops;

	cfg.use_system_calls = 1;

	assert_non_null(ops = ops_alloc(OP_MKDIR, 0, "test", ".", "."));

	assert_failure(perform_operation(OP_MKDIR, ops, NULL, ".", NULL));
	assert_failure(perform_operation(OP_MKDIR, ops, NULL, ".", NULL));
	assert_non_null(strchr(ops->errors, '\n'));

	ops_free(ops);
}

static void
perform_merge(int op)
{
#ifndef _WIN32
	struct stat src, dst;
#endif

	ops_t *ops;

	create_empty_dir("first");
	create_empty_dir("first/nested1");
	create_empty_dir("first/nested1/nested2");
	create_empty_file("first/nested1/nested2/file");

	create_empty_dir("second");
	create_empty_dir("second/nested1");

#ifndef _WIN32
	/* Something about GNU Hurd and OS X differs, so skip this workaround there.
	 * Really need to figure out what's wrong with this thing... */
#if !defined(__gnu_hurd__) && !defined(__APPLE__)
	{
		struct timeval tv[2];
		gettimeofday(&tv[0], NULL);
		tv[1] = tv[0];

		/* This might be Linux-specific, but for the test to work properly access
		 * time should be newer than modification time, in which case it's not
		 * changed on listing directory. */
		tv[0].tv_sec += 3;
		tv[0].tv_usec += 4;
		tv[1].tv_sec += 1;
		tv[1].tv_usec += 2;
		utimes("first/nested1", tv);
	}
#endif
	assert_success(chmod("first/nested1", 0700));
	assert_success(os_stat("first/nested1", &src));
#endif

	cmd_group_begin("undo msg");

	assert_non_null(ops = ops_alloc(op, 0, "merge", ".", "."));
	ops->crp = CRP_OVERWRITE_ALL;
	if(op == OP_MOVEF)
	{
		assert_success(merge_dirs("first", "second", ops));
	}
	else
	{
#ifndef _WIN32
		if(!cfg.use_system_calls)
		{
			assert_success(
					perform_operation(op, ops, NULL, "first/nested1", "second/"));
		}
		else
#endif
		{
			assert_success(perform_operation(op, ops, NULL, "first", "second"));
		}
	}
	ops_free(ops);

	cmd_group_end();

#ifndef _WIN32
	{
		assert_success(os_stat("second/nested1", &dst));
#ifndef HAVE_STRUCT_STAT_ST_MTIM
#define st_atim st_atime
#define st_mtim st_mtime
#endif
		assert_success(memcmp(&src.st_atim, &dst.st_atim, sizeof(src.st_atim)));
		assert_success(memcmp(&src.st_mtim, &dst.st_mtim, sizeof(src.st_mtim)));
		assert_success(memcmp(&src.st_mode, &dst.st_mode, sizeof(src.st_mode)));
	}
#endif

	assert_true(file_exists("second/nested1/nested2/file"));

	assert_success(unlink("second/nested1/nested2/file"));
	assert_success(rmdir("second/nested1/nested2"));
	assert_success(rmdir("second/nested1"));
	assert_success(rmdir("second"));
}

static int
file_exists(const char file[])
{
	return access(file, F_OK) == 0;
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
