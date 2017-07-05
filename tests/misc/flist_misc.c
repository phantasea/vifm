#include <stic.h>

#include <string.h> /* strcpy() */

#include "../../src/cfg/config.h"
#include "../../src/compat/fs_limits.h"
#include "../../src/ui/ui.h"
#include "../../src/utils/fs.h"
#include "../../src/compare.h"
#include "../../src/filelist.h"
#include "../../src/flist_pos.h"

#include "utils.h"

static char cwd[PATH_MAX + 1];

SETUP_ONCE()
{
	assert_non_null(get_cwd(cwd, sizeof(cwd)));
}

SETUP()
{
	curr_view = &lwin;
	other_view = &rwin;

	view_setup(&lwin);

	opt_handlers_setup();
}

TEARDOWN()
{
	view_teardown(&lwin);

	opt_handlers_teardown();

	cfg.ignore_case = 0;
}

TEST(compare_view_defines_id_grouping)
{
	make_abs_path(lwin.curr_dir, sizeof(lwin.curr_dir), TEST_DATA_PATH,
			"compare/a", cwd);
	compare_one_pane(&lwin, CT_CONTENTS, LT_ALL, 0);

	assert_int_equal(3, lwin.list_rows);

	assert_int_equal(0, lwin.list_pos);
	assert_int_equal(1, lwin.list_pos = flist_find_group(&lwin, 1));
	assert_int_equal(2, lwin.list_pos = flist_find_group(&lwin, 1));
	assert_int_equal(1, lwin.list_pos = flist_find_group(&lwin, 0));
}

TEST(goto_file_nagivates_to_files)
{
	make_abs_path(lwin.curr_dir, sizeof(lwin.curr_dir), TEST_DATA_PATH,
			"existing-files", cwd);
	load_dir_list(&lwin, 1);

	cfg.ignore_case = 1;

	assert_int_equal(-1, flist_find_by_ch(&lwin, 'a', 0, 0));
	assert_int_equal(-1, flist_find_by_ch(&lwin, 'A', 1, 0));
	assert_int_equal(0, flist_find_by_ch(&lwin, 'A', 0, 1));
	assert_int_equal(0, flist_find_by_ch(&lwin, 'a', 1, 1));
	assert_int_equal(1, flist_find_by_ch(&lwin, 'b', 0, 0));
	assert_int_equal(-1, flist_find_by_ch(&lwin, 'B', 1, 0));
	assert_int_equal(1, flist_find_by_ch(&lwin, 'B', 0, 1));
	assert_int_equal(1, flist_find_by_ch(&lwin, 'b', 1, 1));
}

TEST(goto_file_nagivates_to_files_with_case_override)
{
	make_abs_path(lwin.curr_dir, sizeof(lwin.curr_dir), TEST_DATA_PATH,
			"existing-files", cwd);
	load_dir_list(&lwin, 1);

	cfg.ignore_case = 1;
	cfg.case_override = CO_GOTO_FILE;
	cfg.case_ignore = 0;

	assert_int_equal(-1, flist_find_by_ch(&lwin, 'a', 0, 0));
	assert_int_equal(-1, flist_find_by_ch(&lwin, 'A', 1, 0));
	assert_int_equal(0, flist_find_by_ch(&lwin, 'A', 0, 1));
	assert_int_equal(0, flist_find_by_ch(&lwin, 'a', 1, 1));
	assert_int_equal(1, flist_find_by_ch(&lwin, 'b', 0, 0));
	assert_int_equal(-1, flist_find_by_ch(&lwin, 'B', 1, 0));
	assert_int_equal(0, flist_find_by_ch(&lwin, 'B', 0, 1));
	assert_int_equal(1, flist_find_by_ch(&lwin, 'b', 1, 1));

	cfg.case_override = 0;
}

TEST(find_directory)
{
	make_abs_path(lwin.curr_dir, sizeof(lwin.curr_dir), TEST_DATA_PATH,
			"tree/dir1", cwd);
	load_dir_list(&lwin, 1);

	assert_int_equal(2, lwin.list_rows);

	assert_int_equal(0, flist_next_dir(&lwin));
	assert_int_equal(0, flist_prev_dir(&lwin));

	lwin.list_pos = 1;

	assert_int_equal(1, flist_next_dir(&lwin));
	assert_int_equal(0, flist_prev_dir(&lwin));
}

TEST(find_selected)
{
	make_abs_path(lwin.curr_dir, sizeof(lwin.curr_dir), TEST_DATA_PATH,
			"existing-files", cwd);
	load_dir_list(&lwin, 1);

	assert_int_equal(3, lwin.list_rows);
	lwin.dir_entry[0].selected = 1;
	lwin.dir_entry[2].selected = 1;
	lwin.selected_files = 2;

	assert_int_equal(2, flist_next_selected(&lwin));
	assert_int_equal(0, flist_prev_selected(&lwin));

	lwin.list_pos = 1;

	assert_int_equal(2, flist_next_selected(&lwin));
	assert_int_equal(0, flist_prev_selected(&lwin));
}

TEST(find_first_and_last_siblings)
{
	make_abs_path(lwin.curr_dir, sizeof(lwin.curr_dir), TEST_DATA_PATH, "tree",
			cwd);
	assert_success(flist_load_tree(&lwin, lwin.curr_dir));
	assert_int_equal(12, lwin.list_rows);

	assert_int_equal(0, flist_first_sibling(&lwin));
	assert_int_equal(11, flist_last_sibling(&lwin));

	lwin.list_pos = 8;

	assert_int_equal(0, flist_first_sibling(&lwin));
	assert_int_equal(11, flist_last_sibling(&lwin));

	lwin.list_pos = 11;
	assert_int_equal(0, flist_first_sibling(&lwin));
	assert_int_equal(11, flist_last_sibling(&lwin));
}

TEST(find_next_and_prev_dir_sibling)
{
	make_abs_path(lwin.curr_dir, sizeof(lwin.curr_dir), TEST_DATA_PATH, "tree",
			cwd);
	assert_success(flist_load_tree(&lwin, lwin.curr_dir));
	assert_int_equal(12, lwin.list_rows);

	assert_int_equal(0, flist_prev_dir_sibling(&lwin));
	assert_int_equal(8, flist_next_dir_sibling(&lwin));

	lwin.list_pos = 8;

	assert_int_equal(0, flist_prev_dir_sibling(&lwin));
	assert_int_equal(8, flist_next_dir_sibling(&lwin));

	lwin.list_pos = 11;

	assert_int_equal(8, flist_prev_dir_sibling(&lwin));
	assert_int_equal(11, flist_next_dir_sibling(&lwin));
}

TEST(find_next_and_prev_mismatches)
{
	curr_view = &lwin;
	other_view = &rwin;

	view_setup(&rwin);

	strcpy(lwin.curr_dir, TEST_DATA_PATH "/compare/a");
	strcpy(rwin.curr_dir, TEST_DATA_PATH "/compare/b");
	(void)compare_two_panes(CT_CONTENTS, LT_ALL, 1, 0);

	assert_int_equal(4, lwin.list_rows);
	assert_int_equal(4, rwin.list_rows);

	lwin.list_pos = 0;

	assert_int_equal(0, flist_prev_mismatch(&lwin));
	assert_int_equal(2, flist_next_mismatch(&lwin));

	lwin.list_pos = 2;

	assert_int_equal(2, flist_prev_mismatch(&lwin));
	assert_int_equal(2, flist_next_mismatch(&lwin));

	lwin.list_pos = 3;

	assert_int_equal(2, flist_prev_mismatch(&lwin));
	assert_int_equal(3, flist_next_mismatch(&lwin));

	view_teardown(&rwin);
}

TEST(current_unselected_file_is_marked)
{
	make_abs_path(lwin.curr_dir, sizeof(lwin.curr_dir), TEST_DATA_PATH,
			"existing-files", cwd);
	load_dir_list(&lwin, 1);

	assert_int_equal(0, lwin.list_pos);
	assert_int_equal(3, lwin.list_rows);

	assert_int_equal(1, mark_selection_or_current(&lwin));

	assert_true(lwin.dir_entry[0].marked);
	assert_false(lwin.dir_entry[1].marked);
	assert_false(lwin.dir_entry[2].marked);
	assert_false(lwin.dir_entry[0].selected);
	assert_false(lwin.dir_entry[1].selected);
	assert_false(lwin.dir_entry[2].selected);
}

TEST(selection_is_marked)
{
	make_abs_path(lwin.curr_dir, sizeof(lwin.curr_dir), TEST_DATA_PATH,
			"existing-files", cwd);
	load_dir_list(&lwin, 1);

	assert_int_equal(0, lwin.list_pos);
	assert_int_equal(3, lwin.list_rows);

	lwin.selected_files = 1;
	lwin.dir_entry[1].selected = 1;
	assert_int_equal(1, mark_selection_or_current(&lwin));

	assert_false(lwin.dir_entry[0].marked);
	assert_true(lwin.dir_entry[1].marked);
	assert_false(lwin.dir_entry[2].marked);
	assert_false(lwin.dir_entry[0].selected);
	assert_true(lwin.dir_entry[1].selected);
	assert_false(lwin.dir_entry[2].selected);
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
