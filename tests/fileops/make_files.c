#include <stic.h>

#include <unistd.h> /* chdir() rmdir() unlink() */

#include "../../src/compat/fs_limits.h"
#include "../../src/ui/ui.h"
#include "../../src/utils/fs.h"
#include "../../src/filelist.h"
#include "../../src/fops_misc.h"

#include "utils.h"

static char *saved_cwd;

SETUP()
{
	saved_cwd = save_cwd();
	assert_success(chdir(SANDBOX_PATH));

	make_abs_path(lwin.curr_dir, sizeof(lwin.curr_dir), SANDBOX_PATH, "",
			saved_cwd);
}

TEARDOWN()
{
	restore_cwd(saved_cwd);
}

TEST(make_files_fails_on_empty_file_name)
{
	char name[] = "";
	char *names[] = { name };

	assert_true(fops_mkfiles(&lwin, -1, names, 1));
}

TEST(make_files_fails_on_file_name_dups)
{
	char name[] = "name";
	char *names[] = { name, name };

	assert_true(fops_mkfiles(&lwin, -1, names, 2));
	assert_failure(unlink(name));
}

TEST(make_files_fails_if_file_exists)
{
	char name[] = "a";
	char *names[] = { name };

	create_empty_file("a");

	assert_true(fops_mkfiles(&lwin, -1, names, 1));

	assert_success(unlink("a"));
}

TEST(make_files_creates_files)
{
	char name_a[] = "a";
	char name_b[] = "b";
	char *names[] = { name_a, name_b };

	(void)fops_mkfiles(&lwin, -1, names, 2);

	assert_success(unlink("a"));
	assert_success(unlink("b"));
}

TEST(make_files_creates_files_by_paths)
{
	char name_a[] = "a";
	char *names[] = { name_a };

	(void)fops_mkfiles(&lwin, -1, names, 1);

	assert_success(unlink("a"));
}

TEST(make_files_considers_tree_structure)
{
	char name[] = "new-file";
	char *names[] = { name };

	view_setup(&lwin);

	create_empty_dir("dir");

	flist_load_tree(&lwin, lwin.curr_dir);

	/* Set at to -1. */
	lwin.list_pos = 0;
	(void)fops_mkfiles(&lwin, -1, names, 1);

	/* Set at to desired position. */
	(void)fops_mkfiles(&lwin, 1, names, 1);

	/* Remove both files afterward to make sure they can both be created at the
	 * same time. */
	assert_success(unlink("new-file"));
	assert_success(unlink("dir/new-file"));

	assert_success(rmdir("dir"));

	view_teardown(&lwin);
}

TEST(check_by_absolute_path_is_performed_beforehand)
{
	char name_a[] = "a";
	char name_b[PATH_MAX + 8];
	char *names[] = { name_a, name_b };

	snprintf(name_b, sizeof(name_b), "%s/b", lwin.curr_dir);
	create_empty_file(name_b);

	(void)fops_mkfiles(&lwin, -1, names, 2);

	assert_failure(unlink("a"));
	assert_success(unlink("b"));
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 : */
