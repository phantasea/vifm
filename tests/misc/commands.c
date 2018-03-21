#include <stic.h>

#include <unistd.h> /* F_OK access() chdir() rmdir() symlink() unlink() */

#include <stdio.h> /* remove() */
#include <string.h> /* strcpy() strdup() */

#include "../../src/compat/fs_limits.h"
#include "../../src/compat/os.h"
#include "../../src/cfg/config.h"
#include "../../src/engine/cmds.h"
#include "../../src/engine/functions.h"
#include "../../src/engine/keys.h"
#include "../../src/modes/modes.h"
#include "../../src/ui/ui.h"
#include "../../src/utils/dynarray.h"
#include "../../src/utils/env.h"
#include "../../src/utils/fs.h"
#include "../../src/utils/path.h"
#include "../../src/utils/str.h"
#include "../../src/builtin_functions.h"
#include "../../src/cmd_core.h"
#include "../../src/filelist.h"
#include "../../src/flist_hist.h"
#include "../../src/ops.h"
#include "../../src/registers.h"
#include "../../src/undo.h"

#include "utils.h"

static int builtin_cmd(const cmd_info_t* cmd_info);

static const cmd_add_t commands[] = {
	{ .name = "builtin",       .abbr = NULL,  .id = -1,      .descr = "descr",
	  .flags = HAS_EMARK | HAS_BG_FLAG,
	  .handler = &builtin_cmd, .min_args = 0, .max_args = 0, },
	{ .name = "onearg",        .abbr = NULL,  .id = -1,      .descr = "descr",
	  .flags = 0,
	  .handler = &builtin_cmd, .min_args = 1, .max_args = 1, },
};

static int called;
static int bg;
static char *arg;
static char *saved_cwd;

static char cwd[PATH_MAX + 1];
static char sandbox[PATH_MAX + 1];
static char test_data[PATH_MAX + 1];

SETUP_ONCE()
{
	assert_non_null(get_cwd(cwd, sizeof(cwd)));

	make_abs_path(sandbox, sizeof(sandbox), SANDBOX_PATH, "", cwd);
	make_abs_path(test_data, sizeof(test_data), TEST_DATA_PATH, "", cwd);
}

SETUP()
{
	view_setup(&lwin);
	view_setup(&rwin);

	curr_view = &lwin;
	other_view = &rwin;

	cfg.cd_path = strdup("");
	cfg.fuse_home = strdup("");
	cfg.slow_fs_list = strdup("");
	cfg.use_system_calls = 1;

#ifndef _WIN32
	replace_string(&cfg.shell, "/bin/sh");
#else
	replace_string(&cfg.shell, "cmd");
#endif

	stats_update_shell_type(cfg.shell);

	init_commands();

	add_builtin_commands(commands, ARRAY_LEN(commands));

	called = 0;

	undo_setup();

	saved_cwd = save_cwd();
}

TEARDOWN()
{
	restore_cwd(saved_cwd);

	update_string(&cfg.cd_path, NULL);
	update_string(&cfg.fuse_home, NULL);
	update_string(&cfg.slow_fs_list, NULL);

	stats_update_shell_type("/bin/sh");
	update_string(&cfg.shell, NULL);

	view_teardown(&lwin);
	view_teardown(&rwin);

	reset_cmds();

	undo_teardown();
}

static int
builtin_cmd(const cmd_info_t* cmd_info)
{
	called = 1;
	bg = cmd_info->bg;

	if(cmd_info->argc != 0)
	{
		replace_string(&arg, cmd_info->argv[0]);
	}

	return 0;
}

TEST(space_amp)
{
	assert_success(exec_commands("builtin &", &lwin, CIT_COMMAND));
	assert_true(called);
	assert_true(bg);
}

TEST(space_amp_spaces)
{
	assert_success(exec_commands("builtin &    ", &lwin, CIT_COMMAND));
	assert_true(called);
	assert_true(bg);
}

TEST(space_bg_bar)
{
	assert_success(exec_commands("builtin &|", &lwin, CIT_COMMAND));
	assert_true(called);
	assert_true(bg);
}

TEST(bg_space_bar)
{
	assert_success(exec_commands("builtin& |", &lwin, CIT_COMMAND));
	assert_true(called);
	assert_true(bg);
}

TEST(space_bg_space_bar)
{
	assert_success(exec_commands("builtin & |", &lwin, CIT_COMMAND));
	assert_true(called);
	assert_true(bg);
}

TEST(non_printable_arg)
{
	/* \x0C is Ctrl-L. */
	assert_success(exec_commands("onearg \x0C", &lwin, CIT_COMMAND));
	assert_true(called);
	assert_string_equal("\x0C", arg);
}

TEST(non_printable_arg_in_udf)
{
	/* \x0C is Ctrl-L. */
	assert_success(exec_commands("command udf :onearg \x0C", &lwin, CIT_COMMAND));

	assert_success(exec_commands("udf", &lwin, CIT_COMMAND));
	assert_true(called);
	assert_string_equal("\x0C", arg);
}

TEST(space_last_arg_in_udf)
{
	assert_success(exec_commands("command udf :onearg \\ ", &lwin, CIT_COMMAND));

	assert_success(exec_commands("udf", &lwin, CIT_COMMAND));
	assert_true(called);
	assert_string_equal(" ", arg);
}

TEST(bg_mark_with_space_in_udf)
{
	assert_success(exec_commands("command udf :builtin &", &lwin, CIT_COMMAND));

	assert_success(exec_commands("udf", &lwin, CIT_COMMAND));
	assert_true(called);
	assert_true(bg);
}

TEST(bg_mark_without_space_in_udf)
{
	assert_success(exec_commands("command udf :builtin&", &lwin, CIT_COMMAND));

	assert_success(exec_commands("udf", &lwin, CIT_COMMAND));
	assert_true(called);
	assert_true(bg);
}

TEST(shell_invocation_works_in_udf)
{
	const char *const cmd = "command! udf echo a > out";

	assert_success(chdir(SANDBOX_PATH));

	assert_success(exec_commands(cmd, &lwin, CIT_COMMAND));

	curr_view = &lwin;

	assert_failure(access("out", F_OK));
	assert_success(exec_commands("udf", &lwin, CIT_COMMAND));
	assert_success(access("out", F_OK));
	assert_success(unlink("out"));
}

TEST(cd_in_root_works)
{
	assert_success(chdir(test_data));

	strcpy(lwin.curr_dir, test_data);

	assert_false(is_root_dir(lwin.curr_dir));
	assert_success(exec_commands("cd /", &lwin, CIT_COMMAND));
	assert_true(is_root_dir(lwin.curr_dir));
}

TEST(double_cd_uses_same_base_for_rel_paths)
{
	char path[PATH_MAX + 1];

	assert_success(chdir(test_data));

	strcpy(lwin.curr_dir, test_data);
	strcpy(rwin.curr_dir, "..");

	assert_success(exec_commands("cd read rename", &lwin, CIT_COMMAND));

	snprintf(path, sizeof(path), "%s/read", test_data);
	assert_true(paths_are_equal(lwin.curr_dir, path));
	snprintf(path, sizeof(path), "%s/rename", test_data);
	assert_true(paths_are_equal(rwin.curr_dir, path));
}

TEST(envvars_of_commands_come_from_variables_unit)
{
	assert_success(chdir(test_data));

	strcpy(lwin.curr_dir, test_data);

	assert_false(is_root_dir(lwin.curr_dir));
	assert_success(exec_commands("let $ABCDE = '/'", &lwin, CIT_COMMAND));
	env_set("ABCDE", SANDBOX_PATH);
	assert_success(exec_commands("cd $ABCDE", &lwin, CIT_COMMAND));
	assert_true(is_root_dir(lwin.curr_dir));
}

TEST(cpmv_does_not_crash_on_wrong_list_access)
{
	char path[PATH_MAX + 1];
	snprintf(path, sizeof(path), "%s/existing-files", test_data);

	assert_success(chdir(path));

	strcpy(lwin.curr_dir, path);
	strcpy(rwin.curr_dir, sandbox);

	lwin.list_rows = 3;
	lwin.list_pos = 0;
	lwin.dir_entry = dynarray_cextend(NULL,
			lwin.list_rows*sizeof(*lwin.dir_entry));
	lwin.dir_entry[0].name = strdup("a");
	lwin.dir_entry[0].origin = &lwin.curr_dir[0];
	lwin.dir_entry[0].selected = 1;
	lwin.dir_entry[1].name = strdup("b");
	lwin.dir_entry[1].origin = &lwin.curr_dir[0];
	lwin.dir_entry[1].selected = 1;
	lwin.dir_entry[2].name = strdup("c");
	lwin.dir_entry[2].origin = &lwin.curr_dir[0];
	lwin.dir_entry[2].selected = 1;
	lwin.selected_files = 3;

	/* cpmv used to use presence of the argument as indication of availability of
	 * file list and access memory beyond array boundaries. */
	(void)exec_commands("co .", &lwin, CIT_COMMAND);

	snprintf(path, sizeof(path), "%s/a", sandbox);
	assert_success(remove(path));
	snprintf(path, sizeof(path), "%s/b", sandbox);
	assert_success(remove(path));
	snprintf(path, sizeof(path), "%s/c", sandbox);
	assert_success(remove(path));
}

TEST(or_operator_is_attributed_to_echo)
{
	(void)exec_commands("echo 1 || builtin", &lwin, CIT_COMMAND);
	assert_false(called);
}

TEST(bar_is_not_attributed_to_echo)
{
	(void)exec_commands("echo 1 | builtin", &lwin, CIT_COMMAND);
	assert_true(called);
}

TEST(mixed_or_operator_and_bar)
{
	(void)exec_commands("echo 1 || 0 | builtin", &lwin, CIT_COMMAND);
	assert_true(called);
}

TEST(or_operator_is_attributed_to_if)
{
	(void)exec_commands("if 0 || 0 | builtin | endif", &lwin, CIT_COMMAND);
	assert_false(called);
}

TEST(or_operator_is_attributed_to_let)
{
	(void)exec_commands("let $a = 'x'", &lwin, CIT_COMMAND);
	assert_string_equal("x", env_get("a"));
	(void)exec_commands("let $a = 0 || 1", &lwin, CIT_COMMAND);
	assert_string_equal("1", env_get("a"));
}

TEST(user_command_is_executed_in_separated_scope)
{
	assert_success(exec_commands("command cmd :if 1 > 2", &lwin, CIT_COMMAND));
	assert_failure(exec_commands("cmd", &lwin, CIT_COMMAND));
}

TEST(tr_extends_second_field)
{
	char path[PATH_MAX + 1];

	assert_success(chdir(sandbox));

	strcpy(lwin.curr_dir, sandbox);

	snprintf(path, sizeof(path), "%s/a b", sandbox);
	create_file(path);

	lwin.list_rows = 1;
	lwin.list_pos = 0;
	lwin.dir_entry = dynarray_cextend(NULL,
			lwin.list_rows*sizeof(*lwin.dir_entry));
	lwin.dir_entry[0].name = strdup("a b");
	lwin.dir_entry[0].origin = &lwin.curr_dir[0];
	lwin.dir_entry[0].selected = 1;
	lwin.selected_files = 1;

	(void)exec_commands("tr/ ?<>\\\\:*|\"/_", &lwin, CIT_COMMAND);

	snprintf(path, sizeof(path), "%s/a_b", sandbox);
	assert_success(remove(path));
}

TEST(putting_files_works)
{
	char path[PATH_MAX + 1];

	regs_init();

	assert_success(os_mkdir(SANDBOX_PATH "/empty-dir", 0700));
	assert_success(flist_load_tree(&lwin, sandbox));

	make_abs_path(path, sizeof(path), TEST_DATA_PATH, "read/binary-data", cwd);
	assert_success(regs_append(DEFAULT_REG_NAME, path));
	lwin.list_pos = 1;

	assert_true(exec_commands("put", &lwin, CIT_COMMAND) != 0);
	restore_cwd(saved_cwd);
	saved_cwd = save_cwd();

	assert_success(unlink(SANDBOX_PATH "/empty-dir/binary-data"));
	assert_success(rmdir(SANDBOX_PATH "/empty-dir"));

	regs_reset();
}

TEST(put_bg_cmd_is_parsed_correctly)
{
	/* Simulate custom view to force failure of the command. */
	lwin.curr_dir[0] = '\0';

	assert_success(exec_commands("put \" &", &lwin, CIT_COMMAND));
}

TEST(wincmd_can_switch_views)
{
	init_modes();
	opt_handlers_setup();
	assert_success(stats_init(&cfg));

	curr_view = &rwin;
	other_view = &lwin;
	assert_success(exec_commands("wincmd h", curr_view, CIT_COMMAND));
	assert_true(curr_view == &lwin);

	curr_view = &rwin;
	other_view = &lwin;
	assert_success(exec_commands("execute 'wincmd h'", curr_view, CIT_COMMAND));
	assert_true(curr_view == &lwin);

	init_builtin_functions();

	curr_view = &rwin;
	other_view = &lwin;
	assert_success(
			exec_commands("if paneisat('left') == 0 | execute 'wincmd h' | endif",
				curr_view, CIT_COMMAND));
	assert_true(curr_view == &lwin);

	curr_view = &rwin;
	other_view = &lwin;
	assert_success(
			exec_commands("if paneisat('left') == 0 "
			             "|    execute 'wincmd h' "
			             "|    let $a = paneisat('left') "
			             "|endif",
				curr_view, CIT_COMMAND));
	assert_true(curr_view == &lwin);
	assert_string_equal("1", env_get("a"));

	function_reset_all();

	opt_handlers_teardown();
	assert_success(stats_reset(&cfg));
	vle_keys_reset();
}

TEST(yank_works_with_ranges)
{
	char path[PATH_MAX + 1];
	reg_t *reg;

	regs_init();

	flist_custom_start(&lwin, "test");
	snprintf(path, sizeof(path), "%s/%s", test_data, "existing-files/a");
	flist_custom_add(&lwin, path);
	assert_true(flist_custom_finish(&lwin, CV_REGULAR, 0) == 0);

	reg = regs_find(DEFAULT_REG_NAME);
	assert_non_null(reg);

	assert_int_equal(0, reg->nfiles);
	(void)exec_commands("%yank", &lwin, CIT_COMMAND);
	assert_int_equal(1, reg->nfiles);

	regs_reset();
}

TEST(symlinks_in_paths_are_not_resolved, IF(not_windows))
{
	char canonic_path[PATH_MAX + 1];
	char buf[PATH_MAX + 1];

	assert_success(os_mkdir(SANDBOX_PATH "/dir1", 0700));
	assert_success(os_mkdir(SANDBOX_PATH "/dir1/dir2", 0700));

	/* symlink() is not available on Windows, but the rest of the code is fine. */
#ifndef _WIN32
	{
		char src[PATH_MAX + 1], dst[PATH_MAX + 1];
		make_abs_path(src, sizeof(src), SANDBOX_PATH, "dir1/dir2", saved_cwd);
		make_abs_path(dst, sizeof(dst), SANDBOX_PATH, "dir-link", saved_cwd);
		assert_success(symlink(src, dst));
	}
#endif

	assert_success(chdir(SANDBOX_PATH "/dir-link"));
	make_abs_path(buf, sizeof(buf), SANDBOX_PATH, "dir-link", saved_cwd);
	to_canonic_path(buf, "/fake-root", lwin.curr_dir,
			sizeof(lwin.curr_dir));
	to_canonic_path(sandbox, "/fake-root", canonic_path,
			sizeof(canonic_path));

	/* :mkdir */
	(void)exec_commands("mkdir ../dir", &lwin, CIT_COMMAND);
	restore_cwd(saved_cwd);
	saved_cwd = save_cwd();
	assert_success(rmdir(SANDBOX_PATH "/dir"));

	/* :clone file name. */
	create_file(SANDBOX_PATH "/dir-link/file");
	populate_dir_list(&lwin, 1);
	(void)exec_commands("clone ../file-clone", &lwin, CIT_COMMAND);
	restore_cwd(saved_cwd);
	saved_cwd = save_cwd();
	assert_success(remove(SANDBOX_PATH "/file-clone"));
	assert_success(remove(SANDBOX_PATH "/dir-link/file"));

	/* :colorscheme */
	make_abs_path(cfg.colors_dir, sizeof(cfg.colors_dir), TEST_DATA_PATH,
			"scripts/", saved_cwd);
	snprintf(buf, sizeof(buf), "colorscheme set-env %s/../dir-link/..",
			sandbox);
	assert_success(exec_commands(buf, &lwin, CIT_COMMAND));
	cs_load_defaults();

	/* :cd */
	assert_success(exec_commands("cd ../dir-link/..", &lwin, CIT_COMMAND));
	assert_string_equal(canonic_path, lwin.curr_dir);

	restore_cwd(saved_cwd);
	saved_cwd = save_cwd();
	assert_success(remove(SANDBOX_PATH "/dir-link"));
	assert_success(rmdir(SANDBOX_PATH "/dir1/dir2"));
	assert_success(rmdir(SANDBOX_PATH "/dir1"));
}

TEST(find_command, IF(not_windows))
{
	opt_handlers_setup();

	replace_string(&cfg.shell, "/bin/sh");

	assert_success(chdir(TEST_DATA_PATH));
	strcpy(lwin.curr_dir, test_data);

	assert_success(exec_commands("set findprg='find %s %a %u'", &lwin,
				CIT_COMMAND));

	/* Nothing to repeat. */
	assert_failure(exec_commands("find", &lwin, CIT_COMMAND));

	assert_success(exec_commands("find a", &lwin, CIT_COMMAND));
	assert_int_equal(3, lwin.list_rows);

	assert_success(exec_commands("find . -name aaa", &lwin, CIT_COMMAND));
	assert_int_equal(1, lwin.list_rows);

	assert_success(exec_commands("find -name '*.vifm'", &lwin, CIT_COMMAND));
	assert_int_equal(4, lwin.list_rows);

	view_teardown(&lwin);
	view_setup(&lwin);

	/* Repeat last search. */
	strcpy(lwin.curr_dir, test_data);
	assert_success(exec_commands("find", &lwin, CIT_COMMAND));
	assert_int_equal(4, lwin.list_rows);

	opt_handlers_teardown();
}

TEST(grep_command, IF(not_windows))
{
	opt_handlers_setup();

	replace_string(&cfg.shell, "/bin/sh");

	assert_success(chdir(TEST_DATA_PATH "/scripts"));
	assert_non_null(get_cwd(lwin.curr_dir, sizeof(lwin.curr_dir)));

	assert_success(exec_commands("set grepprg='grep -n -H -r %i %a %s %u'", &lwin,
				CIT_COMMAND));

	/* Nothing to repeat. */
	assert_failure(exec_commands("grep", &lwin, CIT_COMMAND));

	assert_success(exec_commands("grep command", &lwin, CIT_COMMAND));
	assert_int_equal(2, lwin.list_rows);

	/* Repeat last grep. */
	assert_success(exec_commands("grep!", &lwin, CIT_COMMAND));
	assert_int_equal(1, lwin.list_rows);

	opt_handlers_teardown();
}

TEST(touch)
{
	to_canonic_path(SANDBOX_PATH, cwd, lwin.curr_dir, sizeof(lwin.curr_dir));
	(void)exec_commands("touch file", &lwin, CIT_COMMAND);

	assert_success(remove(SANDBOX_PATH "/file"));
}

TEST(compare)
{
	opt_handlers_setup();

	create_file(SANDBOX_PATH "/file");

	to_canonic_path(SANDBOX_PATH, cwd, lwin.curr_dir, sizeof(lwin.curr_dir));

	/* The file is empty so nothing to do when "skipempty" is specified. */
	assert_success(exec_commands("compare ofone skipempty", &lwin, CIT_COMMAND));
	assert_false(flist_custom_active(&lwin));

	(void)exec_commands("compare byname bysize bycontents listall listdups "
			"listunique ofboth ofone groupids grouppaths", &lwin, CIT_COMMAND);
	assert_true(flist_custom_active(&lwin));
	assert_int_equal(CV_REGULAR, lwin.custom.type);

	assert_success(remove(SANDBOX_PATH "/file"));

	opt_handlers_teardown();
}

TEST(screen)
{
	assert_false(cfg.use_term_multiplexer);

	/* :screen toggles the option. */
	assert_success(exec_commands("screen", &lwin, CIT_COMMAND));
	assert_true(cfg.use_term_multiplexer);
	assert_success(exec_commands("screen", &lwin, CIT_COMMAND));
	assert_false(cfg.use_term_multiplexer);

	/* :screen! sets it to on. */
	assert_success(exec_commands("screen!", &lwin, CIT_COMMAND));
	assert_true(cfg.use_term_multiplexer);
	assert_success(exec_commands("screen!", &lwin, CIT_COMMAND));
	assert_true(cfg.use_term_multiplexer);

	cfg.use_term_multiplexer = 0;
}

TEST(map_commands_count_arguments_correctly)
{
	init_modes();

	/* Each map command below should receive two arguments: "\\" and "j". */
	/* Each unmap command below should receive single argument: "\\". */
	assert_success(exec_commands("cmap \\ j", &lwin, CIT_COMMAND));
	assert_success(exec_commands("cnoremap \\ j", &lwin, CIT_COMMAND));
	assert_success(exec_commands("cunmap \\", &lwin, CIT_COMMAND));
	assert_success(exec_commands("dmap \\ j", &lwin, CIT_COMMAND));
	assert_success(exec_commands("dnoremap \\ j", &lwin, CIT_COMMAND));
	assert_success(exec_commands("dunmap \\", &lwin, CIT_COMMAND));
	assert_success(exec_commands("mmap \\ j", &lwin, CIT_COMMAND));
	assert_success(exec_commands("mnoremap \\ j", &lwin, CIT_COMMAND));
	assert_success(exec_commands("munmap \\", &lwin, CIT_COMMAND));
	assert_success(exec_commands("nmap \\ j", &lwin, CIT_COMMAND));
	assert_success(exec_commands("nnoremap \\ j", &lwin, CIT_COMMAND));
	assert_success(exec_commands("nunmap \\", &lwin, CIT_COMMAND));
	assert_success(exec_commands("map \\ j", &lwin, CIT_COMMAND));
	assert_success(exec_commands("noremap \\ j", &lwin, CIT_COMMAND));
	assert_success(exec_commands("unmap \\", &lwin, CIT_COMMAND));
	assert_success(exec_commands("map! \\ j", &lwin, CIT_COMMAND));
	assert_success(exec_commands("noremap! \\ j", &lwin, CIT_COMMAND));
	assert_success(exec_commands("unmap! \\", &lwin, CIT_COMMAND));
	assert_success(exec_commands("qmap \\ j", &lwin, CIT_COMMAND));
	assert_success(exec_commands("qnoremap \\ j", &lwin, CIT_COMMAND));
	assert_success(exec_commands("qunmap \\", &lwin, CIT_COMMAND));
	assert_success(exec_commands("vmap \\ j", &lwin, CIT_COMMAND));
	assert_success(exec_commands("vnoremap \\ j", &lwin, CIT_COMMAND));
	assert_success(exec_commands("vunmap \\", &lwin, CIT_COMMAND));

	vle_keys_reset();
}

TEST(hist_next_and_prev)
{
	/* Emulate proper history initialization (must happen after view
	 * initialization). */
	cfg_resize_histories(10);
	cfg_resize_histories(0);
	cfg_resize_histories(10);

	flist_hist_save(&lwin, sandbox, ".", 0);
	flist_hist_save(&lwin, test_data, ".", 0);

	assert_success(exec_commands("histprev", &lwin, CIT_COMMAND));
	assert_true(paths_are_same(lwin.curr_dir, sandbox));
	assert_success(exec_commands("histnext", &lwin, CIT_COMMAND));
	assert_true(paths_are_same(lwin.curr_dir, test_data));

	cfg_resize_histories(0);
}

TEST(normal_command_does_not_reset_selection)
{
	init_modes();
	opt_handlers_setup();

	lwin.list_rows = 2;
	lwin.list_pos = 0;
	lwin.dir_entry = dynarray_cextend(NULL,
			lwin.list_rows*sizeof(*lwin.dir_entry));
	lwin.dir_entry[0].name = strdup("a");
	lwin.dir_entry[0].origin = &lwin.curr_dir[0];
	lwin.dir_entry[0].selected = 1;
	lwin.dir_entry[1].name = strdup("b");
	lwin.dir_entry[1].origin = &lwin.curr_dir[0];
	lwin.dir_entry[1].selected = 0;
	lwin.selected_files = 1;

	assert_success(exec_commands(":normal! t", &lwin, CIT_COMMAND));
	assert_int_equal(0, lwin.selected_files);
	assert_false(lwin.dir_entry[0].selected);
	assert_false(lwin.dir_entry[1].selected);

	assert_success(exec_commands(":normal! vG\r", &lwin, CIT_COMMAND));
	assert_int_equal(2, lwin.selected_files);
	assert_true(lwin.dir_entry[0].selected);
	assert_true(lwin.dir_entry[1].selected);

	assert_success(exec_commands(":normal! t", &lwin, CIT_COMMAND));
	assert_int_equal(1, lwin.selected_files);
	assert_true(lwin.dir_entry[0].selected);
	assert_false(lwin.dir_entry[1].selected);

	opt_handlers_teardown();
	vle_keys_reset();
}

TEST(goto_command)
{
	char cmd[PATH_MAX*2];

	assert_failure(exec_commands("goto /", &lwin, CIT_COMMAND));
	assert_failure(exec_commands("goto /no-such-path", &lwin, CIT_COMMAND));

	snprintf(cmd, sizeof(cmd), "goto %s/compare", test_data);
	assert_success(exec_commands(cmd, &lwin, CIT_COMMAND));
	assert_true(paths_are_same(lwin.curr_dir, test_data));
	assert_string_equal("compare", get_current_file_name(&lwin));

	assert_success(exec_commands("goto tree", &lwin, CIT_COMMAND));
	assert_true(paths_are_same(lwin.curr_dir, test_data));
	assert_string_equal("tree", get_current_file_name(&lwin));
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
