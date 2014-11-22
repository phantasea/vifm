/* vifm
 * Copyright (C) 2001 Ken Steen.
 * Copyright (C) 2011 xaizek.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "running.h"

#include <curses.h> /* FALSE curs_set() endwin() */

#include <sys/stat.h> /* stat */
#ifndef _WIN32
#include <sys/wait.h> /* WEXITSTATUS() */
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#ifndef ERROR_ELEVATION_REQUIRED /* Windows Vista and later. */
#define ERROR_ELEVATION_REQUIRED 740L
#endif
#endif
#include <unistd.h> /* F_OK X_OK pid_t access() */

#include <assert.h> /* assert() */
#include <errno.h> /* errno */
#include <stddef.h> /* NULL size_t */
#include <stdio.h> /* snprintf() */
#include <stdlib.h> /* EXIT_FAILURE EXIT_SUCCESS malloc() free() */
#include <string.h> /* strcmp() strerror() strrchr() strcat() strstr() strlen()
                       strchr() strdup() strncmp() */

#include "cfg/config.h"
#include "cfg/info.h"
#include "menus/menus.h"
#include "utils/env.h"
#include "utils/fs.h"
#include "utils/fs_limits.h"
#include "utils/log.h"
#include "utils/path.h"
#include "utils/str.h"
#include "utils/test_helpers.h"
#include "utils/utils.h"
#include "background.h"
#include "file_magic.h"
#include "filelist.h"
#include "filetype.h"
#include "fuse.h"
#include "macros.h"
#include "status.h"
#include "types.h"
#include "ui.h"
#include "vifm.h"

static int is_runnable(const FileView *const view, const char full_path[],
		int type, int force_follow);
static int is_executable(const char full_path[], const dir_entry_t *curr,
		int dont_execute, int runnable);
static int is_dir_entry(const char full_path[], int type);
#ifdef _WIN32
static void run_win_executable(char full_path[]);
static int run_win_executable_as_evaluated(const char full_path[]);
#endif
static int selection_is_consistent(const FileView *const view);
static void execute_file(const char full_path[]);
static void run_selection(FileView *view, int dont_execute);
static void run_file(FileView *view, int dont_execute);
static int multi_run_compat(FileView *view, const char *program);
TSTATIC char * format_edit_selection_cmd(int *bg);
static void follow_link(FileView *view, int follow_dirs);
static void extract_last_path_component(const char path[], char buf[]);
static char * gen_shell_cmd(const char cmd[], int pause,
		int use_term_multiplexer);
static char * gen_term_multiplexer_cmd(const char cmd[], int pause);
static char * gen_term_multiplexer_title_arg(const char cmd[]);
static char * gen_normal_cmd(const char cmd[], int pause);
static char * gen_term_multiplexer_run_cmd(void);
static void set_pwd_in_screen(const char path[]);
static int try_run_with_filetype(FileView *view, const assoc_records_t assocs,
		const char start[], int background);

void
handle_file(FileView *view, int dont_execute, int force_follow)
{
	char full_path[PATH_MAX];
	int executable;
	int runnable;
	const dir_entry_t *const curr = &view->dir_entry[view->list_pos];

	snprintf(full_path, sizeof(full_path), "%s/%s", view->curr_dir, curr->name);

	if(is_dir(full_path) || is_unc_root(view->curr_dir))
	{
		if(!curr->selected && (curr->type != LINK || !force_follow))
		{
			handle_dir(view);
			return;
		}
	}

	runnable = is_runnable(view, full_path, curr->type, force_follow);
	executable = is_executable(full_path, curr, dont_execute, runnable);

	if(curr_stats.file_picker_mode && (executable || runnable))
	{
		/* The call below does not return. */
		vifm_return_file_list(view, 0, NULL);
	}

	if(executable && !is_dir_entry(full_path, curr->type))
	{
		execute_file(full_path);
	}
	else if(runnable)
	{
		run_selection(view, dont_execute);
	}
	else if(curr->type == LINK)
	{
		follow_link(view, force_follow);
	}
}

/* Returns non-zero if file can be executed or it's link to a directory (it can
 * be entered), otherwise zero is returned. */
static int
is_runnable(const FileView *const view, const char full_path[], int type,
		int force_follow)
{
	int runnable = !cfg.follow_links && type == LINK &&
		get_symlink_type(full_path) != SLT_DIR;
	if(runnable && force_follow)
	{
		runnable = 0;
	}
	if(view->selected_files > 0)
	{
		runnable = 1;
	}
	if(!runnable)
	{
		runnable = type == REGULAR || type == EXECUTABLE || type == DIRECTORY;
	}
	return runnable;
}

/* Returns non-zero if file can be executed, otherwise zero is returned. */
static int
is_executable(const char full_path[], const dir_entry_t *curr, int dont_execute,
		int runnable)
{
	int executable;
#ifndef _WIN32
	executable = curr->type == EXECUTABLE ||
			(runnable && access(full_path, X_OK) == 0 && S_ISEXE(curr->mode));
#else
	executable = curr->type == EXECUTABLE;
#endif
	return executable && !dont_execute && cfg.auto_execute;
}

/* Returns non-zero if entry is directory or link to a directory, otherwise zero
 * is returned. */
static int
is_dir_entry(const char full_path[], int type)
{
	return type == DIRECTORY || (type == LINK && is_dir(full_path));
}

#ifdef _WIN32

/* Runs a Windows executable handling errors and rights elevation. */
static void
run_win_executable(char full_path[])
{
	int running_error = 0;
	int running_error_code = NO_ERROR;
	if(curr_stats.as_admin && is_vista_and_above())
	{
		running_error = run_win_executable_as_evaluated(full_path);
	}
	else
	{
		int returned_exit_code;
		const int error = win_exec_cmd(full_path, &returned_exit_code);
		if(error != 0 && !returned_exit_code)
		{
			if(error == ERROR_ELEVATION_REQUIRED && is_vista_and_above())
			{
				const int user_response = query_user_menu("Program running error",
						"Executable requires rights elevation. Run with elevated rights?");
				if(user_response != 0)
				{
					running_error = run_win_executable_as_evaluated(full_path);
				}
			}
			else
			{
				running_error = 1;
				running_error_code = error;
			}
		}
		update_screen(UT_FULL);
	}
	if(running_error)
	{
		char err_msg[512];
		err_msg[0] = '\0';
		if(running_error_code != NO_ERROR && FormatMessageA(
				FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM, NULL,
				running_error_code, 0, err_msg, sizeof(err_msg), NULL) == 0)
		{
			LOG_WERROR(GetLastError());
		}

		show_error_msgf("Program running error", "Can't run an executable%s%s",
				(err_msg[0] == '\0') ? "." : ": ", err_msg);
	}
}

/* Returns non-zero on error, otherwise zero is returned. */
static int
run_win_executable_as_evaluated(const char full_path[])
{
	SHELLEXECUTEINFOA sei;
	memset(&sei, 0, sizeof(sei));
	sei.cbSize = sizeof(sei);
	sei.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOCLOSEPROCESS;
	sei.lpVerb = "runas";
	sei.lpFile = full_path;
	sei.lpParameters = NULL;
	sei.nShow = SW_SHOWNORMAL;

	if(!ShellExecuteEx(&sei))
	{
		const DWORD last_error = GetLastError();
		LOG_WERROR(last_error);
		return last_error != ERROR_CANCELLED;
	}
	CloseHandle(sei.hProcess);
	return 0;
}

#endif /* _WIN32 */

/* Returns non-zero if selection doesn't mix files and directories, otherwise
 * zero is returned. */
static int
selection_is_consistent(const FileView *const view)
{
	if(view->selected_files > 1)
	{
		int files = 0, dirs = 0;
		int i;
		for(i = 0; i < view->list_rows; i++)
		{
			char full[PATH_MAX];
			const dir_entry_t *const curr = &view->dir_entry[i];
			if(!curr->selected)
			{
				continue;
			}

			snprintf(full, sizeof(full), "%s/%s", view->curr_dir, curr->name);
			if(is_dir_entry(full, curr->type))
			{
				dirs++;
			}
			else
			{
				files++;
			}
		}
		if(dirs > 0 && files > 0)
		{
			return 0;
		}
	}
	return 1;
}

/* Executes file, specified by the full_path.  Changes type of slashes on
 * Windows. */
static void
execute_file(const char full_path[])
{
#ifndef _WIN32
	char *const escaped = escape_filename(full_path, 0);
	shellout(escaped, 1, 1);
	free(escaped);
#else
	char *const dquoted_full_path = strdup(enclose_in_dquotes(full_path));

	to_back_slash(dquoted_full_path);
	run_win_executable(dquoted_full_path);

	free(dquoted_full_path);
#endif
}

/* Tries to run selection displaying error message on file type
 * inconsistency. */
static void
run_selection(FileView *view, int dont_execute)
{
	if(selection_is_consistent(view))
	{
		run_file(view, dont_execute);
	}
	else
	{
		show_error_msg("Selection error",
				"Selection cannot contain files and directories at the same time");
	}
}

static void
run_file(FileView *view, int dont_execute)
{
	/* TODO: refactor this function run_file() */

	char *typed_fname;
	assoc_record_t program = {};
	int undef;
	int same;
	dir_entry_t *entry;
	int no_multi_run = 0;

	if(!view->dir_entry[view->list_pos].selected)
		clean_selected_files(view);

	typed_fname = get_typed_current_fname(view);
	(void)get_default_program_for_file(typed_fname, &program);
	free(typed_fname);

	no_multi_run += !multi_run_compat(view, program.command);
	undef = 0;
	same = 1;

	entry = NULL;
	while(iter_selected_entries(view, &entry))
	{
		assoc_record_t prog;
		char *typed_fname;
		int has_def_prog;

		if(!path_exists(entry->name))
		{
			show_error_msgf("Broken Link", "Destination of \"%s\" link doesn't exist",
					entry->name);
			free_assoc_record(&program);
			return;
		}

		typed_fname = get_typed_entry_fname(entry);
		has_def_prog = get_default_program_for_file(typed_fname, &prog);
		free(typed_fname);

		if(!has_def_prog)
		{
			++undef;
			continue;
		}

		no_multi_run += !multi_run_compat(view, prog.command);
		if(assoc_prog_is_empty(&program))
		{
			free_assoc_record(&program);
			program = prog;
		}
		else
		{
			if(strcmp(prog.command, program.command) != 0)
			{
				same = 0;
			}
			free_assoc_record(&prog);
		}
	}

	if(!same && undef == 0 && no_multi_run)
	{
		free_assoc_record(&program);
		show_error_msg("Selection error", "Files have different programs");
		return;
	}
	if(undef > 0)
	{
		free_assoc_record(&program);
	}

	/* Check for a filetype */
	/* vi is set as the default for any extension without a program */
	if(program.command == NULL)
	{
		if(view->dir_entry[view->list_pos].type == DIRECTORY)
		{
			handle_dir(view);
		}
		else if(view->selected_files <= 1)
		{
			char buf[PATH_MAX];
			snprintf(buf, sizeof(buf), "%s/%s", view->curr_dir,
					get_current_file_name(view));
			(void)view_file(buf, -1, -1, 1);
		}
		else
		{
			if(edit_selection() != 0)
			{
				show_error_msg("Running error", "Can't edit selection");
			}
		}
		return;
	}

	if(!no_multi_run)
	{
		dir_entry_t *entry;

		const int pos = view->list_pos;

		free_assoc_record(&program);

		entry = NULL;
		while(iter_selected_entries(view, &entry))
		{
			char *typed_fname;

			typed_fname = get_typed_entry_fname(entry);
			(void)get_default_program_for_file(typed_fname, &program);
			free(typed_fname);

			view->list_pos = entry_to_pos(view, entry);
			run_using_prog(view, program.command, dont_execute, 0);

			free_assoc_record(&program);
		}

		view->list_pos = pos;
	}
	else
	{
		run_using_prog(view, program.command, dont_execute, 0);
		free_assoc_record(&program);
	}
}

static int
multi_run_compat(FileView *view, const char *program)
{
	size_t len;
	if(program == NULL)
		return 0;
	if(view->selected_files <= 1)
		return 0;
	if((len = strlen(program)) == 0)
		return 0;
	if(program[len - 1] != '&')
		return 0;
	if(strstr(program, "%f") != NULL || strstr(program, "%F") != NULL)
		return 0;
	if(strstr(program, "%c") == NULL && strstr(program, "%C") == NULL)
		return 0;
	return 1;
}

int
view_file(const char filename[], int line, int column, int allow_forking)
{
	char vicmd[PATH_MAX];
	char command[PATH_MAX + 5] = "";
	const char *fork_str = allow_forking ? "" : "--nofork";
	char *escaped;
	int bg;
	int result;

	if(!path_exists(filename))
	{
		if(access(filename, F_OK) != 0)
		{
			show_error_msg("Broken Link", "Link destination doesn't exist");
		}
		else
		{
			show_error_msg("Wrong Path", "File doesn't exist");
		}
		return 1;
	}

#ifndef _WIN32
	escaped = escape_filename(filename, 0);
#else
	escaped = (char *)enclose_in_dquotes(filename);
#endif

	snprintf(vicmd, sizeof(vicmd), "%s", get_vicmd(&bg));
	(void)trim_right(vicmd);
	if(!allow_forking)
	{
		char *p = strrchr(vicmd, ' ');
		if(p != NULL && strstr(p, "remote"))
		{
			*p = '\0';
		}
	}

	if(line < 0 && column < 0)
		snprintf(command, sizeof(command), "%s %s %s", vicmd, fork_str, escaped);
	else if(column < 0)
		snprintf(command, sizeof(command), "%s %s +%d %s", vicmd, fork_str, line,
				escaped);
	else
		snprintf(command, sizeof(command), "%s %s \"+call cursor(%d, %d)\" %s",
				vicmd, fork_str, line, column, escaped);

#ifndef _WIN32
	free(escaped);
#endif

	if(bg && allow_forking)
	{
		result = start_background_job(command, 0);
	}
	else
	{
		result = shellout(command, -1, allow_forking);
	}
	curs_set(FALSE);

	return result;
}

int
edit_selection(void)
{
	int error = 1;
	int bg;
	char *const cmd = format_edit_selection_cmd(&bg);
	if(cmd != NULL)
	{
		/* TODO: move next line to a separate function. */
		error = bg ? start_background_job(cmd, 0) : shellout(cmd, -1, 1);
		free(cmd);
	}
	return error;
}

/* Formats a command to edit selected files of the current view in an editor.
 * Returns a newly allocated string, which should be freed by the caller. */
TSTATIC char *
format_edit_selection_cmd(int *bg)
{
	char *const files = expand_macros("%f", NULL, NULL, 1);
	char *const cmd = format_str("%s %s", get_vicmd(bg), files);
	free(files);
	return cmd;
}

void
run_using_prog(FileView *view, const char *program, int dont_execute,
		int force_background)
{
	int pause = starts_with(program, "!!");
	if(pause)
		program += 2;

	if(!path_exists_at(view->curr_dir, view->dir_entry[view->list_pos].name))
	{
		show_error_msg("Access Error", "File doesn't exist.");
		return;
	}

	if(has_mount_prefixes(program))
	{
		if(dont_execute)
		{
			char buf[PATH_MAX];
			snprintf(buf, sizeof(buf), "%s/%s", view->curr_dir,
					get_current_file_name(view));
			(void)view_file(buf, -1, -1, 1);
		}
		else
			fuse_try_mount(view, program);
	}
	else if(strcmp(program, VIFM_PSEUDO_CMD) == 0)
	{
		handle_dir(view);
	}
	else if(strchr(program, '%') != NULL)
	{
		int background;
		MacroFlags flags;
		char *command = expand_macros(program, NULL, &flags, 1);

		background = ends_with(command, " &");
		if(background)
			command[strlen(command) - 2] = '\0';

		if(!pause && (background || force_background))
			start_background_job(command, flags == MACRO_IGNORE);
		else if(flags == MACRO_IGNORE)
			output_to_nowhere(command);
		else
			shellout(command, pause ? 1 : -1, flags != MACRO_NO_TERM_MUX);

		free(command);
	}
	else
	{
		char buf[NAME_MAX + 1 + NAME_MAX + 1];
		const char *name_macro;
		char *file_name;

#ifdef _WIN32
		if(curr_stats.shell_type == ST_CMD)
		{
			name_macro = (view == curr_view) ? "%\"c" : "%\"C";
		}
		else
#endif
		{
			name_macro = (view == curr_view) ? "%c" : "%C";
		}

		file_name = expand_macros(name_macro, NULL, NULL, 1);

		snprintf(buf, sizeof(buf), "%s %s", program, file_name);
		shellout(buf, pause ? 1 : -1, 1);

		free(file_name);
	}
}

static void
follow_link(FileView *view, int follow_dirs)
{
	/* TODO: refactor this big function follow_link() */

	struct stat target_stat;
	char *dir = NULL, *file = NULL, *link_dup;
	char full_path[PATH_MAX];
	char linkto[PATH_MAX + NAME_MAX];
	const char *filename;

	filename = view->dir_entry[view->list_pos].name;
	snprintf(full_path, sizeof(full_path), "%s/%s", view->curr_dir, filename);

	if(get_link_target(full_path, linkto, sizeof(linkto)) != 0)
	{
		show_error_msg("Error", "Can't read link");
		return;
	}

	if(!path_exists(linkto))
	{
		show_error_msg("Broken Link",
				"Can't access link destination. It might be broken");
		return;
	}

	chosp(linkto);

	if(lstat(linkto, &target_stat) != 0)
	{
		show_error_msgf("Link Follow", "Can't stat link destination \"%s\": %s",
				linkto, strerror(errno));
		return;
	}

	link_dup = strdup(linkto);

	if((target_stat.st_mode & S_IFMT) == S_IFDIR && !follow_dirs)
	{
		dir = strdup(filename);
	}
	else
	{
		int i;
		for(i = strlen(linkto) - 1; i > 0; i--)
		{
			if(linkto[i] == '/')
			{
				linkto[i] = '\0';

				if(!is_dir(linkto))
				{
					/* This seems to have something to do with Windows. */
					strcat(linkto, "/");

					if(!is_dir(linkto))
					{
						break;
					}
				}

				dir = strdup(linkto);
				break;
			}
		}

		if((file = strrchr(link_dup, '/')) != NULL)
			++file;
		else if(dir == NULL)
			file = link_dup;
	}
	if(dir != NULL)
	{
		navigate_to(view, dir);
	}
	if(file != NULL)
	{
		const int pos = find_file_pos_in_list(view, file);
		if(pos >= 0)
		{
			move_to_list_pos(view, pos);
		}
	}
	free(link_dup);
	free(dir);
}

void
handle_dir(FileView *view)
{
	char full_path[PATH_MAX];
	char *filename;

	filename = get_current_file_name(view);

	if(is_parent_dir(filename))
	{
		cd_updir(view);
		return;
	}

	snprintf(full_path, sizeof(full_path), "%s%s%s", view->curr_dir,
			ends_with_slash(view->curr_dir) ? "" : "/", filename);
	if(cd_is_possible(full_path))
	{
		navigate_to(view, filename);
	}
}

void
cd_updir(FileView *view)
{
	char dir_name[strlen(view->curr_dir) + 1];

	dir_name[0] = '\0';

	extract_last_path_component(view->curr_dir, dir_name);

	if(change_directory(view, "../") != 1)
	{
		load_dir_list(view, 0);
		move_to_list_pos(view, find_file_pos_in_list(view, dir_name));
	}
}

/* Extracts last part of the path into buf.  Assumes that size of the buf is
 * large enough. */
static void
extract_last_path_component(const char path[], char buf[])
{
	const char *const last = get_last_path_component(path);
	snprintf(buf, until_first(last, '/') - last + 1, "%s", last);
}

/*
 * pause:
 *  > 0 - pause always
 *  = 0 - do not pause
 *  < 0 - pause on error
 */
int
shellout(const char *command, int pause, int use_term_multiplexer)
{
	char *cmd;
	int result;
	int ec;

	if(pause > 0 && command != NULL && ends_with(command, "&"))
	{
		pause = -1;
	}

	cmd = gen_shell_cmd(command, pause > 0, use_term_multiplexer);

	endwin();

	/* Need to use setenv instead of getcwd for a symlink directory */
	env_set("PWD", curr_view->curr_dir);

	ec = vifm_system(cmd);
	/* No WIFEXITED(ec) check here, since vifm_system(...) shouldn't return until
	 * subprocess exited. */
	result = WEXITSTATUS(ec);

	if(result != 0 && pause < 0)
	{
		LOG_ERROR_MSG("Subprocess (%s) exit code: %d (0x%x); status = 0x%x", cmd,
				result, result, ec);
		pause_shell();
	}

	free(cmd);

	/* Force views update. */
	ui_view_schedule_reload(&lwin);
	ui_view_schedule_reload(&rwin);

	recover_after_shellout();

	if(!curr_stats.skip_shellout_redraw)
	{
		/* Redraw to handle resizing of terminal that we could have missed. */
		curr_stats.need_update = UT_FULL;
	}

	curs_set(FALSE);

	return result;
}

/* Composes shell command to run basing on parameters for execution.  NULL cmd
 * parameter opens shell.  Returns a newly allocated string, which should be
 * freed by the caller. */
static char *
gen_shell_cmd(const char cmd[], int pause, int use_term_multiplexer)
{
	char *shell_cmd = NULL;

	if(cmd != NULL)
	{
		if(use_term_multiplexer && curr_stats.term_multiplexer != TM_NONE)
		{
			shell_cmd = gen_term_multiplexer_cmd(cmd, pause);
		}
		else
		{
			shell_cmd = gen_normal_cmd(cmd, pause);
		}
	}
	else if(use_term_multiplexer)
	{
		shell_cmd = gen_term_multiplexer_run_cmd();
	}

	if(shell_cmd == NULL)
	{
		shell_cmd = strdup(cfg.shell);
	}

	return shell_cmd;
}

/* Composes command to be run using terminal multiplexer.  Returns newly
 * allocated string that should be freed by the caller. */
static char *
gen_term_multiplexer_cmd(const char cmd[], int pause)
{
	char *title_arg;
	char *escaped_sh;
	char *raw_shell_cmd;
	char *escaped_shell_cmd;
	char *shell_cmd = NULL;

	if(curr_stats.term_multiplexer != TM_TMUX &&
			curr_stats.term_multiplexer != TM_SCREEN)
	{
		assert(0 && "Unexpected active terminal multiplexer value.");
		return NULL;
	}

	escaped_sh = escape_filename(cfg.shell, 0);

	title_arg = gen_term_multiplexer_title_arg(cmd);

	raw_shell_cmd = format_str("%s%s", cmd, pause ? PAUSE_STR : "");
	escaped_shell_cmd = escape_filename(raw_shell_cmd, 0);

	if(curr_stats.term_multiplexer == TM_TMUX)
	{
		char *const arg = format_str("%s -c %s", escaped_sh, escaped_shell_cmd);
		char *const escaped_arg = escape_filename(arg, 0);

		shell_cmd = format_str("tmux new-window %s %s", title_arg, escaped_arg);

		free(escaped_arg);
		free(arg);
	}
	else if(curr_stats.term_multiplexer == TM_SCREEN)
	{
		set_pwd_in_screen(curr_view->curr_dir);

		shell_cmd = format_str("screen %s %s -c %s", title_arg, escaped_sh,
				escaped_shell_cmd);
	}
	else
	{
		assert(0 && "Unsupported terminal multiplexer type.");
	}

	free(escaped_shell_cmd);
	free(raw_shell_cmd);
	free(title_arg);
	free(escaped_sh);

	return shell_cmd;
}

/* Composes title for window of a terminal multiplexer from a command.  Returns
 * newly allocated string that should be freed by the caller. */
static char *
gen_term_multiplexer_title_arg(const char cmd[])
{
	int bg;
	const char *const vicmd = get_vicmd(&bg);
	const char *const visubcmd = strstr(cmd, vicmd);
	char *command_name = NULL;
	const char *title;
	char *title_arg;

	if(visubcmd != NULL)
	{
		title = skip_whitespace(visubcmd + strlen(vicmd) + 1);
	}
	else
	{
		char *const separator = strchr(cmd, ' ');
		if(separator != NULL)
		{
			*separator = '\0';
			command_name = strdup(cmd);
			*separator = ' ';
		}
		title = command_name;
	}

	if(is_null_or_empty(title))
	{
		title_arg = strdup("");
	}
	else
	{
		const char opt_c = (curr_stats.term_multiplexer == TM_SCREEN) ? 't' : 'n';
		char *const escaped_title = escape_filename(title, 0);
		title_arg = format_str("-%c %s", opt_c, escaped_title);
		free(escaped_title);
	}

	free(command_name);

	return title_arg;
}

/* Composes command to be run without terminal multiplexer.  Returns a newly
 * allocated string, which should be freed by the caller. */
static char *
gen_normal_cmd(const char cmd[], int pause)
{
	if(pause)
	{
		const char *cmd_with_pause_fmt;

#ifdef _WIN32
		if(curr_stats.shell_type == ST_CMD)
		{
			cmd_with_pause_fmt = "%s" PAUSE_STR;
		}
		else
#endif
		{
			cmd_with_pause_fmt = "%s; " PAUSE_CMD;
		}

		return format_str(cmd_with_pause_fmt, cmd);
	}
	else
	{
		return strdup(cmd);
	}
}

/* Composes shell command to run active terminal multiplexer.  Returns a newly
 * allocated string, which should be freed by the caller. */
static char *
gen_term_multiplexer_run_cmd(void)
{
	char *shell_cmd = NULL;

	if(curr_stats.term_multiplexer == TM_SCREEN)
	{
		set_pwd_in_screen(curr_view->curr_dir);

		shell_cmd = strdup("screen");
	}
	else if(curr_stats.term_multiplexer == TM_TMUX)
	{
		shell_cmd = strdup("tmux new-window");
	}
	else
	{
		assert(0 && "Unexpected active terminal multiplexer value.");
	}

	return shell_cmd;
}

/* Changes $PWD in running GNU/screen session to the specified path.  Needed for
 * symlink directories and sshfs mounts. */
static void
set_pwd_in_screen(const char path[])
{
	char *const escaped_dir = escape_filename(path, 0);
	char *const set_pwd = format_str("screen -X setenv PWD %s", escaped_dir);

	(void)vifm_system(set_pwd);

	free(set_pwd);
	free(escaped_dir);
}

void
output_to_nowhere(const char *cmd)
{
	FILE *file, *err;

	if(background_and_capture((char *)cmd, &file, &err) == (pid_t)-1)
	{
		show_error_msgf("Trouble running command", "Unable to run: %s", cmd);
		return;
	}

	fclose(file);
	fclose(err);
}

int
run_with_filetype(FileView *view, const char beginning[], int background)
{
	char *const typed_fname = get_typed_current_fname(view);
	assoc_records_t ft = get_all_programs_for_file(typed_fname);
	assoc_records_t magic = get_magic_handlers(typed_fname);
	free(typed_fname);

	if(try_run_with_filetype(view, ft, beginning, background))
	{
		free(ft.list);
		return 0;
	}

	free(ft.list);

	return !try_run_with_filetype(view, magic, beginning, background);
}

/* Returns non-zero on successful running. */
static int
try_run_with_filetype(FileView *view, const assoc_records_t assocs,
		const char start[], int background)
{
	const size_t len = strlen(start);
	int i;
	for(i = 0; i < assocs.count; i++)
	{
		if(strncmp(assocs.list[i].command, start, len) == 0)
		{
			run_using_prog(view, assocs.list[i].command, 0, background);
			return 1;
		}
	}
	return 0;
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 : */
