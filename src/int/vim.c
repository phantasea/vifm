/* vifm
 * Copyright (C) 2014 xaizek.
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

#include "vim.h"

#include <curses.h> /* FALSE curs_set() */

#include <ctype.h> /* isspace() */
#include <errno.h> /* errno */
#include <stdio.h> /* FILE fclose() fprintf() fputs() snprintf() */
#include <stdlib.h> /* EXIT_SUCCESS free() */
#include <string.h> /* strcmp() strlen() strrchr() strstr() */

#include "../cfg/config.h"
#include "../compat/os.h"
#include "../modes/dialogs/msg_dialog.h"
#include "../ui/ui.h"
#include "../utils/fs.h"
#include "../utils/log.h"
#include "../utils/path.h"
#include "../utils/str.h"
#include "../utils/test_helpers.h"
#include "../utils/utils.h"
#include "../background.h"
#include "../filelist.h"
#include "../macros.h"
#include "../running.h"
#include "../status.h"
#include "../vifm.h"

/* File name known to Vim-plugin. */
#define LIST_FILE "vimfiles"

TSTATIC char * format_edit_selection_cmd(int *bg);
static int run_vim(const char cmd[], int bg, int use_term_multiplexer);
TSTATIC void trim_right(char text[]);
static void dump_filenames(const FileView *view, FILE *fp, int nfiles,
		char *files[]);

int
vim_format_help_cmd(const char topic[], char cmd[], size_t cmd_size)
{
	int bg;

#ifndef _WIN32
	char *const escaped_rtp = shell_like_escape(PACKAGE_DATA_DIR, 0);
	char *const escaped_args = shell_like_escape(topic, 0);

	snprintf(cmd, cmd_size,
			"%s -c 'set runtimepath+=%s/vim-doc' -c help\\ %s",
			cfg_get_vicmd(&bg, 1), escaped_rtp, escaped_args);  //mod by sim1

	free(escaped_args);
	free(escaped_rtp);
#else
	char exe_dir[PATH_MAX];
	char *escaped_rtp;

	(void)get_exe_dir(exe_dir, sizeof(exe_dir));
	escaped_rtp = shell_like_escape(exe_dir, 0);

	snprintf(cmd, cmd_size,
			"%s -c \"set runtimepath+=%s/data/vim-doc\" -c \"help %s\" -c only",
			cfg_get_vicmd(&bg, 1), escaped_rtp, topic);  //mod by sim1

	free(escaped_rtp);
#endif

	return bg;
}

int
vim_edit_files(int nfiles, char *files[])
{
	char *cmd = NULL;
	size_t len = 0U;
	int i;
	int bg;
	int error;

	(void)strappend(&cmd, &len, cfg_get_vicmd(&bg, 0));

	for(i = 0; i < nfiles; ++i)
	{
		char *const escaped = shell_like_escape(files[i], 0);
		(void)strappendch(&cmd, &len, ' ');
		(void)strappend(&cmd, &len, escaped);
		free(escaped);
	}

	error = (cmd == NULL || run_vim(cmd, bg, 1));
	free(cmd);

	return error;
}

int
vim_edit_selection(void)
{
	int error = 1;
	int bg;
	char *const cmd = format_edit_selection_cmd(&bg);
	if(cmd != NULL)
	{
		error = run_vim(cmd, bg, 1);
		free(cmd);
	}
	return error;
}

/* Formats a command to edit selected files of the current view in an editor.
 * Returns a newly allocated string, which should be freed by the caller. */
TSTATIC char *
format_edit_selection_cmd(int *bg)
{
	const char *const fmt = (get_env_type() == ET_WIN) ? "%\"f" : "%f";
	char *const files = expand_macros(fmt, NULL, NULL, 1);
	char *const cmd = format_str("%s %s", cfg_get_vicmd(bg, 0), files);
	free(files);
	return cmd;
}

int
vim_view_file(const char filename[], int line, int column, int allow_forking)
{
	char vicmd[PATH_MAX];
	char cmd[PATH_MAX + 5];
	/* Use `-f` and not `--nofork` here to make it for with nvim (and possibly
	 * other flavours of vi). */
	const char *fork_str = allow_forking ? "" : "-f";
	char *escaped;
	int bg;
	int result;

	cmd[0] = '\0';

	if(!path_exists(filename, DEREF))
	{
		if(path_exists(filename, NODEREF))
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
	escaped = shell_like_escape(filename, 0);
#else
	escaped = (char *)enclose_in_dquotes(filename);
#endif

	copy_str(vicmd, sizeof(vicmd), cfg_get_vicmd(&bg, 0));
	trim_right(vicmd);
	if(!allow_forking)
	{
		char *p = strrchr(vicmd, ' ');
		if(p != NULL && strstr(p, "remote"))
		{
			*p = '\0';
		}
	}

	//mod by sim1 for using vimux util
	if (0 == strcmp(vicmd, "vimux"))
	{
		if (line < 0)
			snprintf(cmd, sizeof(cmd), "%s %s", vicmd, escaped);
		else
			snprintf(cmd, sizeof(cmd), "%s -c %d %s", vicmd, line, escaped);
	}
	else
	{
		if(line < 0 && column < 0)
			snprintf(cmd, sizeof(cmd), "%s %s %s", vicmd, fork_str, escaped);
		else if(column < 0)
			snprintf(cmd, sizeof(cmd), "%s %s +%d %s", vicmd, fork_str, line, escaped);
		else
			snprintf(cmd, sizeof(cmd), "%s %s \"+call cursor(%d, %d)\" %s", vicmd,
					fork_str, line, column, escaped);
	}

#ifndef _WIN32
	free(escaped);
#endif

	result = run_vim(cmd, bg && allow_forking, allow_forking);
	curs_set(0);

	return result;
}

/* Removes all trailing whitespace. */
TSTATIC void
trim_right(char str[])
{
	size_t len = strlen(str);
	while(len > 0U && isspace(str[len - 1U]))
	{
		str[--len] = '\0';
	}
}

/* Runs command with specified settings.  Returns exit code of the command. */
static int
run_vim(const char cmd[], int bg, int use_term_multiplexer)
{
	if(bg)
	{
		return start_background_job(cmd, 0);
	}

	return shellout(cmd, PAUSE_ON_ERROR, use_term_multiplexer);
}

int
vim_write_file_list(const FileView *view, int nfiles, char *files[])
{
	FILE *fp;
	const char *const files_out = curr_stats.chosen_files_out;

	if(is_null_or_empty(files_out))
	{
		return 0;
	}

	if(strcmp(files_out, "-") == 0)
	{
		dump_filenames(view, curr_stats.original_stdout, nfiles, files);
		return 0;
	}

	fp = os_fopen(files_out, "w");
	if(fp == NULL)
	{
		LOG_SERROR_MSG(errno, "Can't open file for writing: \"%s\"", files_out);
		return 1;
	}

	dump_filenames(view, fp, nfiles, files);
	fclose(fp);
	return 0;
}

/* Writes list of full paths to files into the file pointed to by fp.  files and
 * nfiles parameters can be used to supply list of file names in the current
 * directory of the view.  Otherwise current selection is used if current files
 * is selected, if current file is not selected it's the only one that is
 * stored. */
static void
dump_filenames(const FileView *view, FILE *fp, int nfiles, char *files[])
{
	/* Break delimiter in it's first character and the rest to be able to insert
	 * null character via "%c%s" format string. */
	const char delim_c = curr_stats.output_delimiter[0];
	const char *const delim_str = curr_stats.output_delimiter[0] == '\0'
	                            ? ""
	                            : curr_stats.output_delimiter + 1;

	if(nfiles == 0)
	{
		dir_entry_t *entry = NULL;
		while(iter_active_area((FileView *)view, &entry))
		{
			fprintf(fp, "%s/%s%c%s", entry->origin, entry->name, delim_c, delim_str);
		}
	}
	else
	{
		int i;
		for(i = 0; i < nfiles; ++i)
		{
			if(is_path_absolute(files[i]))
			{
				fprintf(fp, "%s%c%s", files[i], delim_c, delim_str);
			}
			else
			{
				fprintf(fp, "%s/%s%c%s", view->curr_dir, files[i], delim_c, delim_str);
			}
		}
	}
}

void
vim_write_empty_file_list(void)
{
	FILE *fp;
	const char *const files_out = curr_stats.chosen_files_out;

	if(is_null_or_empty(files_out) || strcmp(files_out, "-") == 0)
	{
		return;
	}

	fp = os_fopen(files_out, "w");
	if(fp != NULL)
	{
		fclose(fp);
	}
	else
	{
		LOG_SERROR_MSG(errno, "Can't truncate file: \"%s\"", files_out);
	}
}

void
vim_write_dir(const char path[])
{
	/* TODO: move this and other non-Vim related code to extern.c unit. */

	FILE *fp;
	const char *const dir_out = curr_stats.chosen_dir_out;

	if(is_null_or_empty(dir_out))
	{
		return;
	}

	if(strcmp(dir_out, "-") == 0)
	{
		fputs(path, curr_stats.original_stdout);
		return;
	}

	fp = os_fopen(dir_out, "w");
	if(fp == NULL)
	{
		LOG_SERROR_MSG(errno, "Can't open file for writing: \"%s\"", dir_out);
		return;
	}

	fputs(path, fp);
	fclose(fp);
}

int
vim_run_choose_cmd(const FileView *view)
{
	char *expanded_cmd;

	if(is_null_or_empty(curr_stats.on_choose))
	{
		return 0;
	}

	if(!view->dir_entry[view->list_pos].selected)
	{
		erase_selection(curr_view);
	}

	expanded_cmd = expand_macros(curr_stats.on_choose, NULL, NULL, 1);
	if(vifm_system(expanded_cmd) != EXIT_SUCCESS)
	{
		free(expanded_cmd);
		return 1;
	}

	return 0;
}

void
vim_get_list_file_path(char buf[], size_t buf_size)
{
	snprintf(buf, buf_size, "%s/" LIST_FILE, cfg.config_dir);
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
