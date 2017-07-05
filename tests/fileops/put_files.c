#include <stic.h>

#include <sys/stat.h> /* stat */
#include <unistd.h> /* stat() rmdir() unlink() */

#include <string.h> /* strcpy() */

#include "../../src/compat/fs_limits.h"
#include "../../src/utils/fs.h"
#include "../../src/utils/path.h"
#include "../../src/filelist.h"
#include "../../src/fops_common.h"
#include "../../src/fops_misc.h"
#include "../../src/fops_put.h"
#include "../../src/registers.h"
#include "../../src/trash.h"

#include "utils.h"

static void line_prompt(const char prompt[], const char filename[],
		fo_prompt_cb cb, fo_complete_cmd_func complete, int allow_ee);
static void line_prompt_rec(const char prompt[], const char filename[],
		fo_prompt_cb cb, fo_complete_cmd_func complete, int allow_ee);
static char options_prompt_rename(const char title[], const char message[],
		const struct response_variant *variants);
static char options_prompt_rename_rec(const char title[], const char message[],
		const struct response_variant *variants);
static char options_prompt_overwrite(const char title[], const char message[],
		const struct response_variant *variants);
static char options_prompt_abort(const char title[], const char message[],
		const struct response_variant *variants);
static char cm_overwrite(const char title[], const char message[],
		const struct response_variant *variants);
static char cm_no(const char title[], const char message[],
		const struct response_variant *variants);
static char cm_skip(const char title[], const char message[],
		const struct response_variant *variants);
static void parent_overwrite_with_put(int move);
static void double_clash_with_put(int move);

static fo_prompt_cb rename_cb;

static char *saved_cwd;

SETUP()
{
	saved_cwd = save_cwd();

	regs_init();

	make_abs_path(lwin.curr_dir, sizeof(lwin.curr_dir), SANDBOX_PATH, "",
			saved_cwd);

	rename_cb = NULL;
}

TEARDOWN()
{
	regs_reset();
	restore_cwd(saved_cwd);
}

static void
line_prompt(const char prompt[], const char filename[], fo_prompt_cb cb,
		fo_complete_cmd_func complete, int allow_ee)
{
	cb("b");
}

static void
line_prompt_rec(const char prompt[], const char filename[], fo_prompt_cb cb,
		fo_complete_cmd_func complete, int allow_ee)
{
	rename_cb = cb;
}

static char
options_prompt_rename(const char title[], const char message[],
		const struct response_variant *variants)
{
	fops_init(&line_prompt, &options_prompt_overwrite);
	return 'r';
}

static char
options_prompt_rename_rec(const char title[], const char message[],
		const struct response_variant *variants)
{
	fops_init(&line_prompt_rec, &options_prompt_overwrite);
	return 'r';
}

static char
options_prompt_overwrite(const char title[], const char message[],
		const struct response_variant *variants)
{
	return 'o';
}

static char
options_prompt_abort(const char title[], const char message[],
		const struct response_variant *variants)
{
	return '\x03';
}

static char
cm_overwrite(const char title[], const char message[],
		const struct response_variant *variants)
{
	fops_init(&line_prompt, &cm_no);
	return 'o';
}

static char
cm_no(const char title[], const char message[],
		const struct response_variant *variants)
{
	fops_init(&line_prompt, &cm_skip);
	return 'n';
}

static char
cm_skip(const char title[], const char message[],
		const struct response_variant *variants)
{
	fops_init(&line_prompt, &options_prompt_overwrite);
	return 's';
}

TEST(put_files_bg_fails_on_wrong_register)
{
	assert_true(fops_put_bg(&lwin, -1, -1, 0));
	wait_for_bg();
}

TEST(put_files_bg_fails_on_empty_register)
{
	assert_true(fops_put_bg(&lwin, -1, 'a', 0));
	wait_for_bg();
}

TEST(put_files_bg_fails_on_identical_names_in_a_register)
{
	assert_success(regs_append('a', TEST_DATA_PATH "/existing-files/a"));
	assert_success(regs_append('a', TEST_DATA_PATH "/rename/a"));

	assert_true(fops_put_bg(&lwin, -1, 'a', 0));
	wait_for_bg();
}

TEST(put_files_bg_fails_on_file_name_conflict)
{
	create_empty_file(SANDBOX_PATH "/a");

	assert_success(regs_append('a', TEST_DATA_PATH "/rename/a"));

	assert_true(fops_put_bg(&lwin, -1, 'a', 0));
	wait_for_bg();

	assert_success(unlink(SANDBOX_PATH "/a"));
}

TEST(put_files_bg_copies_files)
{
	assert_success(regs_append('a', TEST_DATA_PATH "/existing-files/a"));

	assert_int_equal(0, fops_put_bg(&lwin, -1, 'a', 0));
	wait_for_bg();

	assert_success(unlink(SANDBOX_PATH "/a"));
}

TEST(put_files_bg_skips_nonexistent_source_files)
{
	create_empty_dir(SANDBOX_PATH "/dir");
	create_empty_file(SANDBOX_PATH "/dir/b");

	assert_success(regs_append('a', TEST_DATA_PATH "/existing-files/a"));
	assert_success(regs_append('a', SANDBOX_PATH "/dir/b"));
	assert_success(unlink(SANDBOX_PATH "/dir/b"));

	assert_int_equal(0, fops_put_bg(&lwin, -1, 'a', 0));
	wait_for_bg();

	assert_success(unlink(SANDBOX_PATH "/a"));
	assert_success(rmdir(SANDBOX_PATH "/dir"));
}

TEST(put_files_bg_demangles_names_of_trashed_files)
{
	char path[PATH_MAX + 1];
	make_abs_path(path, sizeof(path), SANDBOX_PATH, "trash", saved_cwd);

	set_trash_dir(path);

	create_empty_dir(SANDBOX_PATH "/trash");
	create_empty_file(SANDBOX_PATH "/trash/000_b");

	make_abs_path(path, sizeof(path), SANDBOX_PATH, "trash/000_b", saved_cwd);
	assert_success(regs_append('a', path));

	assert_int_equal(0, fops_put_bg(&lwin, -1, 'a', 1));
	wait_for_bg();

	assert_success(unlink(SANDBOX_PATH "/b"));
	assert_success(rmdir(SANDBOX_PATH "/trash"));
}

TEST(put_files_copies_files_according_to_tree_structure)
{
	char path[PATH_MAX + 1];

	view_setup(&lwin);

	create_empty_dir(SANDBOX_PATH "/dir");

	flist_load_tree(&lwin, lwin.curr_dir);

	make_abs_path(path, sizeof(path), TEST_DATA_PATH, "existing-files/a",
			saved_cwd);
	assert_success(regs_append('a', path));

	/* Copy at the top level.  Set at to -1. */

	lwin.list_pos = 0;
	(void)fops_put(&lwin, -1, 'a', 0);
	restore_cwd(saved_cwd);
	saved_cwd = save_cwd();
	assert_success(unlink(SANDBOX_PATH "/a"));

	lwin.list_pos = 0;
	assert_int_equal(0, fops_put_bg(&lwin, -1, 'a', 0));
	wait_for_bg();
	assert_success(unlink(SANDBOX_PATH "/a"));

	/* Copy at nested level.  Set at to desired position. */

	(void)fops_put(&lwin, 1, 'a', 0);
	restore_cwd(saved_cwd);
	saved_cwd = save_cwd();
	assert_success(unlink(SANDBOX_PATH "/dir/a"));

	/* Here target position in 100, which should become 1 automatically. */
	assert_int_equal(0, fops_put_bg(&lwin, 100, 'a', 0));
	wait_for_bg();
	assert_success(unlink(SANDBOX_PATH "/dir/a"));

	restore_cwd(saved_cwd);
	saved_cwd = save_cwd();
	assert_success(rmdir(SANDBOX_PATH "/dir"));

	view_teardown(&lwin);
}

TEST(overwrite_request_accounts_for_target_file_rename)
{
	struct stat st;
	char src_file[PATH_MAX + 1];

	create_empty_file(SANDBOX_PATH "/binary-data");
	create_empty_file(SANDBOX_PATH "/b");

	make_abs_path(src_file, sizeof(src_file), TEST_DATA_PATH, "read/binary-data",
			saved_cwd);
	assert_success(regs_append('a', src_file));

	fops_init(&line_prompt, &options_prompt_rename);

	(void)fops_put(&lwin, -1, 'a', 0);
	restore_cwd(saved_cwd);
	saved_cwd = save_cwd();

	assert_success(stat(SANDBOX_PATH "/binary-data", &st));
	assert_int_equal(0, st.st_size);

	assert_success(stat(SANDBOX_PATH "/b", &st));
	assert_int_equal(1024, st.st_size);

	(void)remove(SANDBOX_PATH "/binary-data");
	(void)remove(SANDBOX_PATH "/b");
}

TEST(abort_stops_operation)
{
	create_empty_file(SANDBOX_PATH "/a");
	create_empty_dir(SANDBOX_PATH "/dir");
	create_empty_dir(SANDBOX_PATH "/dir/dir");
	create_empty_file(SANDBOX_PATH "/dir/dir/a");
	create_empty_file(SANDBOX_PATH "/dir/b");

	assert_success(regs_append('a', SANDBOX_PATH "/dir/dir/a"));
	assert_success(regs_append('a', SANDBOX_PATH "/dir/b"));

	fops_init(&line_prompt, &options_prompt_abort);
	(void)fops_put(&lwin, -1, 'a', 0);
	restore_cwd(saved_cwd);
	saved_cwd = save_cwd();

	assert_success(unlink(SANDBOX_PATH "/a"));
	assert_failure(unlink(SANDBOX_PATH "/b"));
	assert_success(unlink(SANDBOX_PATH "/dir/dir/a"));
	assert_success(unlink(SANDBOX_PATH "/dir/b"));
	assert_success(rmdir(SANDBOX_PATH "/dir/dir"));
	assert_success(rmdir(SANDBOX_PATH "/dir"));
}

TEST(parent_overwrite_is_prevented_on_file_put_copy)
{
	parent_overwrite_with_put(0);
}

TEST(parent_overwrite_is_prevented_on_file_put_move)
{
	parent_overwrite_with_put(1);
}

TEST(rename_on_put)
{
	char path[PATH_MAX + 1];
	make_abs_path(path, sizeof(path), SANDBOX_PATH, "a", saved_cwd);

	create_empty_file(SANDBOX_PATH "/a");

	assert_success(regs_append('a', path));

	fops_init(&line_prompt_rec, &options_prompt_rename_rec);
	(void)fops_put(&lwin, -1, 'a', 0);
	/* Continue the operation. */
	rename_cb("b");

	restore_cwd(saved_cwd);
	saved_cwd = save_cwd();
	assert_success(remove(SANDBOX_PATH "/a"));
	assert_success(remove(SANDBOX_PATH "/b"));
}

TEST(multiple_clashes_are_resolved_by_user_on_put_copy)
{
	double_clash_with_put(0);
}

TEST(multiple_clashes_are_resolved_by_user_on_put_move)
{
	double_clash_with_put(1);
}

TEST(change_mind)
{
	char path[PATH_MAX + 1];

	create_empty_dir(SANDBOX_PATH "/dir");
	create_empty_dir(SANDBOX_PATH "/dir/dir");
	create_empty_dir(SANDBOX_PATH "/dir/dir/dir");
	create_empty_file(SANDBOX_PATH "/dir/dir/dir/file1");
	create_empty_dir(SANDBOX_PATH "/dir2");
	create_empty_dir(SANDBOX_PATH "/dir2/dir");
	create_empty_dir(SANDBOX_PATH "/dir2/dir/dir");
	create_empty_file(SANDBOX_PATH "/dir2/dir/dir/file2");

	make_abs_path(path, sizeof(path), SANDBOX_PATH, "dir/dir", saved_cwd);
	assert_success(regs_append('a', path));
	make_abs_path(path, sizeof(path), SANDBOX_PATH, "dir2/dir", saved_cwd);
	assert_success(regs_append('a', path));

	/* Overwrite #1 -> No -> Skip -> Overwrite #2. */

	fops_init(&line_prompt, &cm_overwrite);
	(void)fops_put(&lwin, -1, 'a', 0);
	restore_cwd(saved_cwd);
	saved_cwd = save_cwd();

	assert_success(remove(SANDBOX_PATH "/dir2/dir/dir/file2"));
	assert_success(rmdir(SANDBOX_PATH "/dir2/dir/dir"));
	assert_success(rmdir(SANDBOX_PATH "/dir2/dir"));
	assert_success(rmdir(SANDBOX_PATH "/dir2"));
	assert_success(remove(SANDBOX_PATH "/dir/dir/file1"));
	assert_success(rmdir(SANDBOX_PATH "/dir/dir"));
	assert_success(rmdir(SANDBOX_PATH "/dir"));
}

static void
parent_overwrite_with_put(int move)
{
	char path[PATH_MAX + 1];

	create_empty_dir(SANDBOX_PATH "/dir");
	create_empty_dir(SANDBOX_PATH "/dir/dir");
	create_empty_dir(SANDBOX_PATH "/dir/dir1");
	create_empty_file(SANDBOX_PATH "/dir/dir/file");
	create_empty_file(SANDBOX_PATH "/dir/dir1/file2");

	make_abs_path(path, sizeof(path), SANDBOX_PATH, "dir/dir", saved_cwd);
	assert_success(regs_append('a', path));
	make_abs_path(path, sizeof(path), SANDBOX_PATH, "dir/dir1", saved_cwd);
	assert_success(regs_append('a', path));
	make_abs_path(path, sizeof(path), SANDBOX_PATH, "dir/dir1/file2", saved_cwd);
	assert_success(regs_append('a', path));

	fops_init(&line_prompt, &options_prompt_overwrite);
	(void)fops_put(&lwin, -1, 'a', move);
	restore_cwd(saved_cwd);
	saved_cwd = save_cwd();

	assert_success(remove(SANDBOX_PATH "/dir/file"));
	assert_success(remove(SANDBOX_PATH "/file2"));
	assert_success(rmdir(SANDBOX_PATH "/dir"));
	if(!move)
	{
		assert_success(remove(SANDBOX_PATH "/dir1/file2"));
	}
	assert_success(rmdir(SANDBOX_PATH "/dir1"));
}

static void
double_clash_with_put(int move)
{
	char path[PATH_MAX + 1];

	create_empty_dir(SANDBOX_PATH "/dir");
	create_empty_dir(SANDBOX_PATH "/dir/dir");
	create_empty_dir(SANDBOX_PATH "/dir/dir/dir");
	create_empty_file(SANDBOX_PATH "/dir/dir/file1");
	create_empty_file(SANDBOX_PATH "/dir/dir/dir/file2");

	make_abs_path(path, sizeof(path), SANDBOX_PATH, "dir/dir", saved_cwd);
	assert_success(regs_append('a', path));
	make_abs_path(path, sizeof(path), SANDBOX_PATH, "dir/dir/dir", saved_cwd);
	assert_success(regs_append('a', path));

	/* The larger sub-tree should be moved in this case. */

	fops_init(&line_prompt, &options_prompt_overwrite);
	(void)fops_put(&lwin, -1, 'a', move);
	restore_cwd(saved_cwd);
	saved_cwd = save_cwd();

	assert_success(remove(SANDBOX_PATH "/dir/dir/file2"));
	assert_success(remove(SANDBOX_PATH "/dir/file1"));
	assert_success(rmdir(SANDBOX_PATH "/dir/dir"));
	assert_success(rmdir(SANDBOX_PATH "/dir"));
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
