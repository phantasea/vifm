#include <stic.h>

#include <unistd.h> /* chdir() */

#include <string.h> /* strcpy() */

#include "../../src/compat/fs_limits.h"
#include "../../src/ui/ui.h"
#include "../../src/utils/fs.h"

#include "utils.h"

SETUP()
{
	opt_handlers_setup();
}

TEARDOWN()
{
	opt_handlers_teardown();
}

TEST(change_window_swaps_views)
{
	curr_view = &lwin;
	other_view = &rwin;

	change_window();

	assert_true(curr_view == &rwin);
	assert_true(other_view == &lwin);
}

TEST(change_window_updates_pwd)
{
	char expected_cwd[PATH_MAX + 1];
	char cwd[PATH_MAX + 1];

	char *const saved_cwd = save_cwd();

	curr_view = &lwin;
	other_view = &rwin;

	assert_success(chdir(SANDBOX_PATH));
	assert_non_null(get_cwd(expected_cwd, sizeof(expected_cwd)));
	restore_cwd(saved_cwd);

	assert_success(chdir(TEST_DATA_PATH "/existing-files"));
	strcpy(rwin.curr_dir, expected_cwd);

	change_window();

	assert_non_null(get_cwd(cwd, sizeof(cwd)));
	assert_string_equal(expected_cwd, cwd);
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 : */
