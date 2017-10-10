#include <stic.h>

#include "../../src/cfg/config.h"
#include "../../src/engine/cmds.h"
#include "../../src/ui/statusbar.h"
#include "../../src/ui/ui.h"
#include "../../src/utils/matcher.h"
#include "../../src/cmd_core.h"
#include "../../src/filtering.h"

#include "utils.h"

SETUP()
{
	view_setup(&lwin);
	view_setup(&rwin);

	/* Emulate proper history initialization (must happen after view
	 * initialization). */
	cfg_resize_histories(5);
	cfg_resize_histories(0);

	curr_view = &lwin;
	other_view = &rwin;

	init_commands();
}

TEARDOWN()
{
	view_teardown(&lwin);
	view_teardown(&rwin);

	reset_cmds();
}

TEST(filter_prints_empty_filters_correctly)
{
	const char *expected = "Filter -- Flags -- Value\n"
	                       "Local              \n"
	                       "Name               \n"
	                       "Auto               ";

	status_bar_message("");
	assert_failure(exec_commands("filter?", &lwin, CIT_COMMAND));
	assert_string_equal(expected, ui_sb_last());
}

TEST(filter_prints_non_empty_filters)
{
	const char *expected = "Filter -- Flags -- Value\n"
	                       "Local     I        local\n"
	                       "Name      ---->    abc\n"
	                       "Auto               ";

	assert_success(exec_commands("filter abc", &lwin, CIT_COMMAND));
	local_filter_apply(&lwin, "local");

	status_bar_message("");
	assert_failure(exec_commands("filter?", &lwin, CIT_COMMAND));
	assert_string_equal(expected, ui_sb_last());
}

TEST(filter_with_empty_value_reuses_last_search)
{
	const char *expected = "Filter -- Flags -- Value\n"
	                       "Local              \n"
	                       "Name      ---->    /pattern/I\n"
	                       "Auto               ";

	cfg_resize_histories(5);
	cfg_save_search_history("pattern");

	assert_success(exec_commands("filter //I", &lwin, CIT_COMMAND));
	status_bar_message("");
	assert_failure(exec_commands("filter?", &lwin, CIT_COMMAND));
	assert_string_equal(expected, ui_sb_last());
}

TEST(filter_accepts_pipe_without_escaping)
{
	assert_success(exec_commands("filter /a|b/", &lwin, CIT_COMMAND));
	assert_success(exec_commands("filter a|b", &lwin, CIT_COMMAND));
}

TEST(filter_prints_whole_manual_filter_expression)
{
	const char *expected = "Filter -- Flags -- Value\n"
	                       "Local              \n"
	                       "Name      ---->    /abc/i\n"
	                       "Auto               ";

	assert_success(exec_commands("filter /abc/i", &lwin, CIT_COMMAND));

	status_bar_message("");
	assert_failure(exec_commands("filter?", &lwin, CIT_COMMAND));
	assert_string_equal(expected, ui_sb_last());
}

TEST(filter_without_args_resets_manual_filter)
{
	const char *expected = "Filter -- Flags -- Value\n"
	                       "Local              \n"
	                       "Name               \n"
	                       "Auto               ";

	assert_success(exec_commands("filter this", &lwin, CIT_COMMAND));
	assert_success(exec_commands("filter", &lwin, CIT_COMMAND));

	status_bar_message("");
	assert_failure(exec_commands("filter?", &lwin, CIT_COMMAND));
	assert_string_equal(expected, ui_sb_last());
}

TEST(filter_reset_is_not_affected_by_search_history)
{
	const char *expected = "Filter -- Flags -- Value\n"
	                       "Local              \n"
	                       "Name               \n"
	                       "Auto               ";

	cfg_resize_histories(5);
	cfg_save_search_history("pattern");

	assert_success(exec_commands("filter this", &lwin, CIT_COMMAND));
	assert_success(exec_commands("filter", &lwin, CIT_COMMAND));

	status_bar_message("");
	assert_failure(exec_commands("filter?", &lwin, CIT_COMMAND));
	assert_string_equal(expected, ui_sb_last());
}

TEST(filter_can_affect_both_views)
{
	assert_string_equal("", matcher_get_expr(curr_view->manual_filter));
	assert_string_equal("", matcher_get_expr(other_view->manual_filter));
	curr_view->invert = 1;
	other_view->invert = 1;

	curr_stats.global_local_settings = 1;
	assert_success(exec_commands("filter /x/", &lwin, CIT_COMMAND));
	curr_stats.global_local_settings = 0;

	assert_string_equal("/x/", matcher_get_expr(curr_view->manual_filter));
	assert_string_equal("/x/", matcher_get_expr(other_view->manual_filter));
	assert_false(curr_view->invert);
	assert_false(other_view->invert);
}

TEST(filter_can_setup_inverted_filter)
{
	assert_string_equal("", matcher_get_expr(curr_view->manual_filter));
	curr_view->invert = 0;

	assert_success(exec_commands("filter! /x/", &lwin, CIT_COMMAND));

	assert_string_equal("/x/", matcher_get_expr(curr_view->manual_filter));
	assert_true(curr_view->invert);
}

TEST(filter_can_invert_manual_filter)
{
	curr_view->invert = 0;
	assert_success(exec_commands("filter!", &lwin, CIT_COMMAND));
	assert_true(curr_view->invert);
	assert_success(exec_commands("filter!", &lwin, CIT_COMMAND));
	assert_false(curr_view->invert);
}

TEST(filter_accepts_full_path_patterns)
{
	assert_success(exec_commands("filter ///some/path//", &lwin, CIT_COMMAND));
}

TEST(filter_accepts_paths_with_many_spaces)
{
	assert_success(exec_commands("filter { a b c d e }", &lwin, CIT_COMMAND));
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 : */
