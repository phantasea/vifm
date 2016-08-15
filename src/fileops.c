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

#include "fileops.h"

#include <regex.h>

#include <fcntl.h>
#include <sys/stat.h> /* stat */
#include <sys/types.h> /* waitpid() */
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif
#include <unistd.h> /* unlink() */

#include <assert.h> /* assert() */
#include <ctype.h> /* isdigit() tolower() */
#include <errno.h> /* errno */
#include <stddef.h> /* NULL size_t */
#include <stdint.h> /* uint64_t */
#include <stdio.h> /* snprintf() */
#include <stdlib.h> /* calloc() free() malloc() realloc() strtol() */
#include <string.h> /* memcmp() memset() strcat() strcmp() strcpy() strdup()
                       strerror() */

#include "cfg/config.h"
#include "compat/fs_limits.h"
#include "compat/os.h"
#include "compat/reallocarray.h"
#include "int/vim.h"
#include "io/ioeta.h"
#include "io/ionotif.h"
#include "modes/dialogs/msg_dialog.h"
#include "modes/modes.h"
#include "modes/wk.h"
#include "ui/cancellation.h"
#include "ui/fileview.h"
#include "ui/statusbar.h"
#include "ui/ui.h"
#ifdef _WIN32
#include "utils/env.h"
#endif
#include "utils/fs.h"
#include "utils/fsdata.h"
#include "utils/macros.h"
#include "utils/path.h"
#include "utils/regexp.h"
#include "utils/str.h"
#include "utils/string_array.h"
#include "utils/test_helpers.h"
#include "utils/utils.h"
#include "background.h"
#include "cmd_completion.h"
#include "filelist.h"
#include "ops.h"
#include "registers.h"
#include "running.h"
#include "status.h"
#include "trash.h"
#include "types.h"
#include "undo.h"

/* 10 to the power of number of digits after decimal point to take into account
 * on progress percentage counting. */
#define IO_PRECISION 10

/* Key used to switch to progress dialog. */
#define IO_DETAILS_KEY 'i'

/* What to do with rename candidate name (old name and new name). */
typedef enum
{
	RA_SKIP,   /* Skip rename (when new name matches the old one). */
	RA_FAIL,   /* Abort renaming (status bar error was printed). */
	RA_RENAME, /* Rename this file. */
}
RenameAction;

/* Path roles for check_if_dir_writable() function. */
typedef enum
{
	DR_CURRENT,     /* Current (source) path. */
	DR_DESTINATION, /* Destination path. */
}
DirRole;

/* Object for auxiliary information related to progress of operations in
 * io_progress_changed() handler. */
typedef struct
{
	int bg; /* Whether this is background operation. */
	union
	{
		ops_t *ops;     /* Information for foreground operation. */
		bg_op_t *bg_op; /* Information for background operation. */
	};

	int last_progress; /* Progress of the operation during previous call. */
	IoPs last_stage;   /* Stage of the operation during previous call. */

	/* Whether progress is displayed in a dialog, rather than on status bar. */
	int dialog;

	int width; /* Maximum reached width of the dialog. */
}
progress_data_t;

/* Pack of arguments supplied to procedures implementing file operations in
 * background. */
typedef struct
{
	char **list;         /* User supplied list of new file names. */
	int nlines;          /* Number of user supplied file names (list size). */
	int move;            /* Whether this is a move operation. */
	int force;           /* Whether destination files should be removed. */
	char **sel_list;     /* Full paths of files to be processed. */
	size_t sel_list_len; /* Number of files to process (sel_list size). */
	char path[PATH_MAX]; /* Path at which processing should take place. */
	int from_file;       /* Whether list was read from a file. */
	int use_trash;       /* Whether either source or destination is trash
	                        directory. */
	char *is_in_trash;   /* Flags indicating whether i-th file is in trash.  Can
	                        be NULL when unused. */
	ops_t *ops;          /* Pointer to pre-allocated operation description. */
}
bg_args_t;

/* Arguments pack for dir_size_bg() background function. */
typedef struct
{
	char *path; /* Full path to directory to process, will be freed. */
	int force;  /* Whether cached values should be ignored. */
}
dir_size_args_t;

static void io_progress_changed(const io_progress_t *const state);
static int calc_io_progress(const io_progress_t *const state, int *skip);
static void io_progress_fg(const io_progress_t *const state, int progress);
static void io_progress_fg_sb(const io_progress_t *const state, int progress);
static void io_progress_bg(const io_progress_t *const state, int progress);
static char * format_file_progress(const ioeta_estim_t *estim, int precision);
static void format_pretty_path(const char base_dir[], const char path[],
		char pretty[], size_t pretty_size);
static int prepare_register(int reg);
static void delete_files_in_bg(bg_op_t *bg_op, void *arg);
static void delete_file_in_bg(ops_t *ops, const char path[], int use_trash);
TSTATIC int is_name_list_ok(int count, int nlines, char *list[], char *files[]);
static int perform_renaming(FileView *view, char *files[], char is_dup[],
		int len, char *dst[]);
TSTATIC int is_rename_list_ok(char *files[], char is_dup[], int len,
		char *list[]);
TSTATIC const char * incdec_name(const char fname[], int k);
static int count_digits(int number);
TSTATIC int check_file_rename(const char dir[], const char old[],
		const char new[], SignalType signal_type);
#ifndef _WIN32
static int complete_owner(const char str[], void *arg);
#endif
static int is_file_name_changed(const char old[], const char new[]);
static void change_owner_cb(const char new_owner[]);
#ifndef _WIN32
static int complete_group(const char str[], void *arg);
#endif
static int complete_filename(const char str[], void *arg);
TSTATIC int merge_dirs(const char src[], const char dst[], ops_t *ops);
static void put_confirm_cb(const char dest_name[]);
static void prompt_what_to_do(const char src_name[]);
static void handle_prompt_response(const char fname[], char response);
static void put_files_in_bg(bg_op_t *bg_op, void *arg);
TSTATIC const char * gen_clone_name(const char normal_name[]);
static char ** grab_marked_files(FileView *view, size_t *nmarked);
static int clone_file(const dir_entry_t *entry, const char path[],
		const char clone[], ops_t *ops);
static void put_continue(int force);
static int is_dir_entry(const char full_path[], const struct dirent* dentry);
static int initiate_put_files(FileView *view, int at, CopyMoveLikeOp op,
		const char descr[], int reg_name);
static OPS cmlo_to_op(CopyMoveLikeOp op);
static void reset_put_confirm(OPS main_op, const char descr[],
		const char dst_dir[]);
static int put_files_i(FileView *view, int start);
static int put_next(int force);
static RenameAction check_rename(const char old_fname[], const char new_fname[],
		char **dest, int ndest);
static int rename_marked(FileView *view, const char desc[], const char lhs[],
		const char rhs[], char **dest);
static void fixup_entry_after_rename(FileView *view, dir_entry_t *entry,
		const char new_fname[]);
static int enqueue_marked_files(ops_t *ops, FileView *view,
		const char dst_hint[], int to_trash);
static ops_t * get_ops(OPS main_op, const char descr[], const char base_dir[],
		const char target_dir[]);
static void progress_msg(const char text[], int ready, int total);
static int cpmv_prepare(FileView *view, char ***list, int *nlines,
		CopyMoveLikeOp op, int force, char undo_msg[], size_t undo_msg_len,
		char dst_path[], size_t dst_path_len, int *from_file);
static int can_read_selected_files(FileView *view);
static int check_dir_path(const FileView *view, const char path[], char buf[],
		size_t buf_len);
static char ** edit_list(size_t count, char **orig, int *nlines,
		int ignore_change);
static int edit_file(const char filepath[], int force_changed);
static const char * cmlo_to_str(CopyMoveLikeOp op);
static void cpmv_files_in_bg(bg_op_t *bg_op, void *arg);
static void bg_ops_init(ops_t *ops, bg_op_t *bg_op);
static ops_t * get_bg_ops(OPS main_op, const char descr[], const char dir[]);
static progress_data_t * alloc_progress_data(int bg, void *info);
static void free_ops(ops_t *ops);
static void cpmv_file_in_bg(ops_t *ops, const char src[], const char dst[],
		int move, int force, int from_trash, const char dst_dir[]);
static int mv_file(const char src[], const char src_dir[], const char dst[],
		const char dst_dir[], OPS op, int cancellable, ops_t *ops);
static int mv_file_f(const char src[], const char dst[], OPS op, int bg,
		int cancellable, ops_t *ops);
static int cp_file(const char src_dir[], const char dst_dir[], const char src[],
		const char dst[], CopyMoveLikeOp op, int cancellable, ops_t *ops);
static int cp_file_f(const char src[], const char dst[], CopyMoveLikeOp op,
		int bg, int cancellable, ops_t *ops);
static void free_bg_args(bg_args_t *args);
static void general_prepare_for_bg_task(FileView *view, bg_args_t *args);
static void append_marked_files(FileView *view, char buf[], char **fnames);
static void append_fname(char buf[], size_t len, const char fname[]);
static const char * get_cancellation_suffix(void);
static int can_add_files_to_view(const FileView *view, int at);
static const char * get_top_dir(const FileView *view);
static const char * get_dst_dir(const FileView *view, int at);
static int check_if_dir_writable(DirRole dir_role, const char path[]);
static void update_dir_entry_size(const FileView *view, int index, int force);
static void start_dir_size_calc(const char path[], int force);
static void dir_size_bg(bg_op_t *bg_op, void *arg);
static void dir_size(char path[], int force);
static void redraw_after_path_change(FileView *view, const char path[]);

/* Temporary storage for extension of file being renamed in name-only mode. */
static char rename_file_ext[NAME_MAX];

/* Global state for file putting and name conflicts resolution that happen in
 * the process. */
static struct
{
	reg_t *reg;        /* Register used for the operation. */
	FileView *view;    /* View in which operation takes place. */
	CopyMoveLikeOp op; /* Type of current operation. */
	int index;         /* Index of the next file of the register to process. */
	int processed;     /* Number of successfully processed files. */
	int skip_all;      /* Skip all conflicting files/directories. */
	int overwrite_all; /* Overwrite all future conflicting files/directories. */
	int append;        /* Whether we're appending ending of a file or not. */
	int allow_merge;   /* Allow merging of files in directories. */
	int merge;         /* Merge conflicting directory once. */
	int merge_all;     /* Merge all conflicting directories. */
	ops_t *ops;        /* Currently running operation. */
	char *dest_name;   /* Name of destination file. */
	char *dest_dir;    /* Destination path. */
}
put_confirm;

/* Filename editing function. */
static line_prompt_func line_prompt;
/* Function to choose from one of options. */
static options_prompt_func options_prompt;

void
init_fileops(line_prompt_func line_func, options_prompt_func options_func)
{
	line_prompt = line_func;
	options_prompt = options_func;
	ionotif_register(&io_progress_changed);
}

/* I/O operation update callback. */
static void
io_progress_changed(const io_progress_t *const state)
{
	const ioeta_estim_t *const estim = state->estim;
	progress_data_t *const pdata = estim->param;

	int redraw = 0;
	int progress, skip;

	progress = calc_io_progress(state, &skip);
	if(skip)
	{
		return;
	}

	/* Don't query for scheduled redraw or input for background operations. */
	if(!pdata->bg)
	{
		redraw = fetch_redraw_scheduled();

		if(!pdata->dialog)
		{
			if(ui_char_pressed(IO_DETAILS_KEY))
			{
				pdata->dialog = 1;
				clean_status_bar();
			}
		}
	}

	/* Do nothing if progress change is small, but force update on stage
	 * change or redraw request. */
	if(progress == pdata->last_progress &&
			state->stage == pdata->last_stage && !redraw)
	{
		return;
	}

	pdata->last_stage = state->stage;

	if(progress >= 0)
	{
		pdata->last_progress = progress;
	}

	if(redraw)
	{
		modes_redraw();
	}

	if(pdata->bg)
	{
		io_progress_bg(state, progress);
	}
	else
	{
		io_progress_fg(state, progress);
	}
}

/* Calculates current IO operation progress.  *skip will be set to non-zero
 * value to indicate that progress change is irrelevant.  Returns progress in
 * the range [-1; 100], where -1 means "unknown". */
static int
calc_io_progress(const io_progress_t *const state, int *skip)
{
	const ioeta_estim_t *const estim = state->estim;
	progress_data_t *const pdata = estim->param;

	*skip = 0;
	if(state->stage == IO_PS_ESTIMATING)
	{
		return estim->total_items/IO_PRECISION;
	}
	else if(estim->total_bytes == 0)
	{
		return 0;
	}
	else if(pdata->last_progress >= 100*IO_PRECISION &&
			estim->current_byte == estim->total_bytes)
	{
		/* Special handling for unknown total size. */
		++pdata->last_progress;
		if(pdata->last_progress%IO_PRECISION != 0)
		{
			*skip = 1;
		}
		return -1;
	}
	else
	{
		return (estim->current_byte*100*IO_PRECISION)/estim->total_bytes;
	}
}

/* Takes care of progress for foreground operations. */
static void
io_progress_fg(const io_progress_t *const state, int progress)
{
	char current_size_str[16];
	char total_size_str[16];
	char src_path[PATH_MAX];
	const char *title, *ctrl_msg;
	const char *target_name;
	char *as_part;
	const char *item_name;
	int item_num;

	const ioeta_estim_t *const estim = state->estim;
	progress_data_t *const pdata = estim->param;
	ops_t *const ops = pdata->ops;

	if(!pdata->dialog)
	{
		io_progress_fg_sb(state, progress);
		return;
	}

	(void)friendly_size_notation(estim->total_bytes, sizeof(total_size_str),
			total_size_str);

	copy_str(src_path, sizeof(src_path), replace_home_part(estim->item));
	remove_last_path_component(src_path);

	title = ops_describe(ops);
	ctrl_msg = "Press Ctrl-C to cancel";
	if(state->stage == IO_PS_ESTIMATING)
	{
		char pretty_path[PATH_MAX];
		format_pretty_path(ops->base_dir, estim->item, pretty_path,
				sizeof(pretty_path));
		draw_msgf(title, ctrl_msg, pdata->width,
				"In %s\nestimating...\nItems: %" PRINTF_ULL "\n"
				"Overall: %s\nCurrent: %s",
				ops->target_dir, (unsigned long long)estim->total_items, total_size_str,
				pretty_path);
		pdata->width = getmaxx(error_win);
		return;
	}

	(void)friendly_size_notation(estim->current_byte,
			sizeof(current_size_str), current_size_str);

	item_name = get_last_path_component(estim->item);

	target_name = get_last_path_component(estim->target);
	if(stroscmp(target_name, item_name) == 0)
	{
		as_part = strdup("");
	}
	else
	{
		as_part = format_str("\nas   %s", target_name);
	}

	item_num = MIN(estim->current_item + 1, estim->total_items);

	if(progress < 0)
	{
		/* Simplified message for unknown total size. */
		draw_msgf(title, ctrl_msg, pdata->width,
				"Location: %s\nItem:     %d of %" PRINTF_ULL "\n"
				"Overall:  %s\n"
				" \n" /* Space is on purpose to preserve empty line. */
				"file %s\nfrom %s%s",
				replace_home_part(ops->target_dir), item_num,
				(unsigned long long)estim->total_items, total_size_str, item_name,
				src_path, as_part);
	}
	else
	{
		char *const file_progress = format_file_progress(estim, IO_PRECISION);

		draw_msgf(title, ctrl_msg, pdata->width,
				"Location: %s\nItem:     %d of %" PRINTF_ULL "\n"
				"Overall:  %s/%s (%2d%%)\n"
				" \n" /* Space is on purpose to preserve empty line. */
				"file %s\nfrom %s%s%s",
				replace_home_part(ops->target_dir), item_num,
				(unsigned long long)estim->total_items, current_size_str,
				total_size_str, progress/IO_PRECISION, item_name, src_path, as_part,
				file_progress);

		free(file_progress);
	}
	pdata->width = getmaxx(error_win);

	free(as_part);
}

/* Takes care of progress for foreground operations displayed on status line. */
static void
io_progress_fg_sb(const io_progress_t *const state, int progress)
{
	const ioeta_estim_t *const estim = state->estim;
	progress_data_t *const pdata = estim->param;
	ops_t *const ops = pdata->ops;

	char current_size_str[16];
	char total_size_str[16];
	char pretty_path[PATH_MAX];
	char *suffix;

	(void)friendly_size_notation(estim->total_bytes, sizeof(total_size_str),
			total_size_str);

	format_pretty_path(ops->base_dir, estim->item, pretty_path,
			sizeof(pretty_path));

	switch(state->stage)
	{
		case IO_PS_ESTIMATING:
			suffix = format_str("estimating... %" PRINTF_ULL "; %s %s",
					(unsigned long long)estim->total_items, total_size_str, pretty_path);
			break;
		case IO_PS_IN_PROGRESS:
			(void)friendly_size_notation(estim->current_byte,
					sizeof(current_size_str), current_size_str);

			if(progress < 0)
			{
				/* Simplified message for unknown total size. */
				suffix = format_str("%" PRINTF_ULL " of %" PRINTF_ULL "; %s %s",
						(unsigned long long)estim->current_item + 1U,
						(unsigned long long)estim->total_items, total_size_str,
						pretty_path);
			}
			else
			{
				suffix = format_str("%" PRINTF_ULL " of %" PRINTF_ULL "; "
						"%s/%s (%2d%%) %s",
						(unsigned long long)estim->current_item + 1,
						(unsigned long long)estim->total_items, current_size_str,
						total_size_str, progress/IO_PRECISION, pretty_path);
			}
			break;

		default:
			assert(0 && "Unhandled progress stage");
			suffix = strdup("");
			break;
	}

	ui_sb_quick_msgf("(hit %c for details) %s: %s", IO_DETAILS_KEY,
			ops_describe(ops), suffix);
	free(suffix);
}

/* Takes care of progress for background operations. */
static void
io_progress_bg(const io_progress_t *const state, int progress)
{
	const ioeta_estim_t *const estim = state->estim;
	progress_data_t *const pdata = estim->param;
	bg_op_t *const bg_op = pdata->bg_op;

	bg_op->progress = progress/IO_PRECISION;
	bg_op_changed(bg_op);
}

/* Formats file progress part of the progress message.  Returns pointer to newly
 * allocated memory. */
static char *
format_file_progress(const ioeta_estim_t *estim, int precision)
{
	char current_size[16];
	char total_size[16];

	const int file_progress = (estim->total_file_bytes == 0U) ? 0 :
		(estim->current_file_byte*100*precision)/estim->total_file_bytes;

	if(estim->total_items == 1)
	{
		return strdup("");
	}

	(void)friendly_size_notation(estim->current_file_byte, sizeof(current_size),
			current_size);
	(void)friendly_size_notation(estim->total_file_bytes, sizeof(total_size),
			total_size);

	return format_str("\nprogress %s/%s (%2d%%)", current_size, total_size,
			file_progress/precision);
}

/* Pretty prints path shortening it by skipping base directory path if
 * possible, otherwise fallbacks to the full path. */
static void
format_pretty_path(const char base_dir[], const char path[], char pretty[],
		size_t pretty_size)
{
	if(!path_starts_with(path, base_dir))
	{
		copy_str(pretty, pretty_size, path);
		return;
	}

	copy_str(pretty, pretty_size, skip_char(path + strlen(base_dir), '/'));
}

int
yank_files(FileView *view, int reg)
{
	int nyanked_files;
	dir_entry_t *entry;

	reg = prepare_register(reg);

	nyanked_files = 0;
	entry = NULL;
	while(iter_marked_entries(view, &entry))
	{
		char full_path[PATH_MAX];
		get_full_path_of(entry, sizeof(full_path), full_path);

		if(regs_append(reg, full_path) == 0)
		{
			++nyanked_files;
		}
	}

	regs_update_unnamed(reg);

	status_bar_messagef("%d file%s yanked", nyanked_files,
			(nyanked_files == 1) ? "" : "s");

	return 1;
}

/* Fills undo message buffer with list of files.  buf should be at least
 * COMMAND_GROUP_INFO_LEN characters length. */
static void
get_group_file_list(char **list, int count, char buf[])
{
	size_t len;
	int i;

	len = strlen(buf);
	for(i = 0; i < count && len < COMMAND_GROUP_INFO_LEN; i++)
	{
		append_fname(buf, len, list[i]);
		len = strlen(buf);
	}
}

int
delete_files(FileView *view, int reg, int use_trash)
{
	char undo_msg[COMMAND_GROUP_INFO_LEN];
	int i;
	dir_entry_t *entry;
	int nmarked_files;
	ops_t *ops;
	const char *const top_dir = get_top_dir(view);
	const char *const curr_dir = top_dir == NULL ? flist_get_dir(view) : top_dir;

	if(!can_change_view_files(view))
	{
		return 0;
	}

	use_trash = use_trash && cfg.use_trash;

	/* This check for the case when we are for sure in the trash. */
	if(use_trash && top_dir != NULL && is_under_trash(top_dir))
	{
		show_error_msg("Can't perform deletion",
				"Current directory is under trash directory");
		return 0;
	}

	if(use_trash)
	{
		reg = prepare_register(reg);
	}

	snprintf(undo_msg, sizeof(undo_msg), "%celete in %s: ", use_trash ? 'd' : 'D',
			replace_home_part(curr_dir));
	append_marked_files(view, undo_msg, NULL);
	cmd_group_begin(undo_msg);

	ops = get_ops(OP_REMOVE, use_trash ? "deleting" : "Deleting", curr_dir,
			curr_dir);

	ui_cancellation_reset();

	nmarked_files = enqueue_marked_files(ops, view, NULL, use_trash);

	entry = NULL;
	i = 0;
	while(iter_marked_entries(view, &entry) && !ui_cancellation_requested())
	{
		char full_path[PATH_MAX];
		int result;

		get_full_path_of(entry, sizeof(full_path), full_path);

		progress_msg("Deleting files", i++, nmarked_files);

		if(use_trash)
		{
			if(is_trash_directory(full_path))
			{
				show_error_msg("Can't perform deletion",
						"You cannot delete trash directory to trash");
				result = -1;
			}
			else if(is_under_trash(full_path))
			{
				show_error_msgf("Skipping file deletion",
						"File is already in trash: %s", full_path);
				result = -1;
			}
			else
			{
				char *const dest = gen_trash_name(entry->origin, entry->name);
				if(dest != NULL)
				{
					result = perform_operation(OP_MOVE, ops, NULL, full_path, dest);
					/* For some reason "rm" sometimes returns 0 on cancellation. */
					if(path_exists(full_path, DEREF))
					{
						result = -1;
					}
					if(result == 0)
					{
						add_operation(OP_MOVE, NULL, NULL, full_path, dest);
						regs_append(reg, dest);
					}
					free(dest);
				}
				else
				{
					show_error_msgf("No trash directory is available",
							"Either correct trash directory paths or prune files.  "
							"Deletion failed on: %s", entry->name);
					result = -1;
				}
			}
		}
		else
		{
			result = perform_operation(OP_REMOVE, ops, NULL, full_path, NULL);
			/* For some reason "rm" sometimes returns 0 on cancellation. */
			if(path_exists(full_path, DEREF))
			{
				result = -1;
			}
			if(result == 0)
			{
				add_operation(OP_REMOVE, NULL, NULL, full_path, "");
			}
		}

		if(result == 0 && entry_to_pos(view, entry) == view->list_pos)
		{
			if(view->list_pos + 1 < view->list_rows)
			{
				++view->list_pos;
			}
		}

		ops_advance(ops, result == 0);
	}

	regs_update_unnamed(reg);

	cmd_group_end();

	ui_view_reset_selection_and_reload(view);

	status_bar_messagef("%d %s %celeted%s", ops->succeeded,
			(ops->succeeded == 1) ? "file" : "files", use_trash ? 'd' : 'D',
			get_cancellation_suffix());

	free_ops(ops);

	return 1;
}

/* Transforms "A-"Z register to "a-"z or clears the reg.  So that for "A-"Z new
 * values will be appended to "a-"z, for other registers old values will be
 * removed.  Returns possibly modified value of the reg parameter. */
static int
prepare_register(int reg)
{
	if(reg >= 'A' && reg <= 'Z')
	{
		reg += 'a' - 'A';
	}
	else
	{
		regs_clear(reg);
	}
	return reg;
}

int
delete_files_bg(FileView *view, int use_trash)
{
	char task_desc[COMMAND_GROUP_INFO_LEN];
	bg_args_t *args;
	unsigned int i;
	const char *const top_dir = get_top_dir(view);
	const char *const curr_dir = top_dir == NULL ? flist_get_dir(view) : top_dir;

	if(!can_change_view_files(view))
	{
		return 0;
	}

	use_trash = use_trash && cfg.use_trash;

	if(use_trash && top_dir != NULL && is_under_trash(top_dir))
	{
		show_error_msg("Can't perform deletion",
				"Current directory is under trash directory");
		return 0;
	}

	args = calloc(1, sizeof(*args));
	args->use_trash = use_trash;

	general_prepare_for_bg_task(view, args);

	for(i = 0U; i < args->sel_list_len; ++i)
	{
		const char *const full_file_path = args->sel_list[i];
		if(is_trash_directory(full_file_path))
		{
			show_error_msg("Can't perform deletion",
					"You cannot delete trash directory to trash");
			free_bg_args(args);
			return 0;
		}
		else if(is_under_trash(full_file_path))
		{
			show_error_msgf("Skipping file deletion", "File is already in trash: %s",
					full_file_path);
			free_bg_args(args);
			return 0;
		}
	}

	if(cfg_confirm_delete(use_trash))
	{
		const char *const title = use_trash ? "Deletion" : "Permanent deletion";
		char perm_del_msg[512];

		snprintf(perm_del_msg, sizeof(perm_del_msg),
				"Are you sure about removing %ld file%s?",
				(long)args->sel_list_len, (args->sel_list_len == 1) ? "" : "s");

		if(!prompt_msg(title, perm_del_msg))
		{
			free_bg_args(args);
			return 0;
		}
	}

	move_cursor_out_of(view, FLS_MARKING);

	snprintf(task_desc, sizeof(task_desc), "%celete in %s: ",
			use_trash ? 'd' : 'D', replace_home_part(curr_dir));

	append_marked_files(view, task_desc, NULL);

	args->ops = get_bg_ops(use_trash ? OP_REMOVE : OP_REMOVESL,
			use_trash ? "deleting" : "Deleting", args->path);

	if(bg_execute(task_desc, "...", args->sel_list_len, 1, &delete_files_in_bg,
				args) != 0)
	{
		free_bg_args(args);

		show_error_msg("Can't perform deletion",
				"Failed to initiate background operation");
	}
	return 0;
}

/* Entry point for a background task that deletes files. */
static void
delete_files_in_bg(bg_op_t *bg_op, void *arg)
{
	size_t i;
	bg_args_t *const args = arg;
	ops_t *ops = args->ops;
	bg_ops_init(ops, bg_op);

	if(ops->use_system_calls)
	{
		size_t i;
		bg_op_set_descr(bg_op, "estimating...");
		for(i = 0U; i < args->sel_list_len; ++i)
		{
			const char *const src = args->sel_list[i];
			char *trash_dir = args->use_trash ? pick_trash_dir(src) : args->path;
			ops_enqueue(ops, src, trash_dir);
			if(trash_dir != args->path)
			{
				free(trash_dir);
			}
		}
	}

	for(i = 0U; i < args->sel_list_len; ++i)
	{
		const char *const src = args->sel_list[i];
		bg_op_set_descr(bg_op, src);
		delete_file_in_bg(ops, src, args->use_trash);
		++bg_op->done;
	}

	free_bg_args(args);
}

/* Actual implementation of background file removal. */
static void
delete_file_in_bg(ops_t *ops, const char path[], int use_trash)
{
	if(!use_trash)
	{
		(void)perform_operation(OP_REMOVE, ops, (void *)1, path, NULL);
		return;
	}

	if(!is_trash_directory(path))
	{
		const char *const fname = get_last_path_component(path);
		char *const trash_name = gen_trash_name(path, fname);
		const char *const dest = (trash_name != NULL) ? trash_name : fname;
		(void)perform_operation(OP_MOVE, ops, (void *)1, path, dest);
		free(trash_name);
	}
}

static void
rename_file_cb(const char new_name[])
{
	char buf[MAX(COMMAND_GROUP_INFO_LEN, 10 + NAME_MAX + 1)];
	char new[strlen(new_name) + 1 + strlen(rename_file_ext) + 1 + 1];
	int mv_res;
	dir_entry_t *const entry = &curr_view->dir_entry[curr_view->list_pos];
	const char *const fname = entry->name;
	const char *const forigin = entry->origin;

	if(is_null_or_empty(new_name))
	{
		return;
	}

	if(contains_slash(new_name))
	{
		status_bar_error("Name can not contain slash");
		curr_stats.save_msg = 1;
		return;
	}

	snprintf(new, sizeof(new), "%s%s%s", new_name,
			(rename_file_ext[0] == '\0') ? "" : ".", rename_file_ext);

	if(check_file_rename(forigin, fname, new, ST_DIALOG) <= 0)
	{
		return;
	}

	snprintf(buf, sizeof(buf), "rename in %s: %s to %s",
			replace_home_part(forigin), fname, new);
	cmd_group_begin(buf);
	mv_res = mv_file(fname, forigin, new, forigin, OP_MOVE, 1, NULL);
	cmd_group_end();
	if(mv_res != 0)
	{
		show_error_msg("Rename Error", "Rename operation failed");
		return;
	}

	/* Rename file in internal structures for correct positioning of cursor after
	 * reloading, as cursor will be positioned on the file with the same name. */
	fentry_rename(curr_view, entry, new);

	ui_view_schedule_reload(curr_view);
}

static int
complete_filename_only(const char str[], void *arg)
{
	filename_completion(str, CT_FILE_WOE, 0);
	return 0;
}

void
rename_current_file(FileView *view, int name_only)
{
	const char *const old = get_current_file_name(view);
	char filename[strlen(old) + 1];

	if(!can_change_view_files(view))
	{
		return;
	}

	copy_str(filename, sizeof(filename), old);
	if(is_parent_dir(filename))
	{
		show_error_msg("Rename error",
				"You can't rename parent directory this way");
		return;
	}

	if(name_only)
	{
		copy_str(rename_file_ext, sizeof(rename_file_ext), cut_extension(filename));
	}
	else
	{
		rename_file_ext[0] = '\0';
	}

	clean_selected_files(view);
	line_prompt("New name: ", filename, rename_file_cb, complete_filename_only,
			1);
}

TSTATIC int
is_name_list_ok(int count, int nlines, char *list[], char *files[])
{
	int i;

	if(nlines < count)
	{
		status_bar_errorf("Not enough file names (%d/%d)", nlines, count);
		curr_stats.save_msg = 1;
		return 0;
	}

	if(nlines > count)
	{
		status_bar_errorf("Too many file names (%d/%d)", nlines, count);
		curr_stats.save_msg = 1;
		return 0;
	}

	for(i = 0; i < count; ++i)
	{
		chomp(list[i]);

		if(files != NULL)
		{
			char *file_s = find_slashr(files[i]);
			char *list_s = find_slashr(list[i]);
			if(list_s != NULL || file_s != NULL)
			{
				if(list_s - list[i] != file_s - files[i] ||
						strnoscmp(files[i], list[i], list_s - list[i]) != 0)
				{
					if(file_s == NULL)
						status_bar_errorf("Name \"%s\" contains slash", list[i]);
					else
						status_bar_errorf("Won't move \"%s\" file", files[i]);
					curr_stats.save_msg = 1;
					return 0;
				}
			}
		}

		if(list[i][0] != '\0' && is_in_string_array(list, i, list[i]))
		{
			status_bar_errorf("Name \"%s\" duplicates", list[i]);
			curr_stats.save_msg = 1;
			return 0;
		}
	}

	return 1;
}

static char **
add_files_to_list(const char *path, char **files, int *len)
{
	DIR* dir;
	struct dirent* dentry;
	const char* slash = "";

	if(!is_dir(path))
	{
		*len = add_to_string_array(&files, *len, 1, path);
		return files;
	}

	dir = os_opendir(path);
	if(dir == NULL)
		return files;

	if(path[strlen(path) - 1] != '/')
		slash = "/";

	while((dentry = os_readdir(dir)) != NULL)
	{
		if(!is_builtin_dir(dentry->d_name))
		{
			char buf[PATH_MAX];
			snprintf(buf, sizeof(buf), "%s%s%s", path, slash, dentry->d_name);
			files = add_files_to_list(buf, files, len);
		}
	}

	os_closedir(dir);
	return files;
}

int
rename_files(FileView *view, char **list, int nlines, int recursive)
{
	char **files;
	int nfiles;
	dir_entry_t *entry;
	char *is_dup;
	int free_list = 0;

	/* Allow list of names in tests. */
	if(curr_stats.load_stage != 0 && recursive && nlines != 0)
	{
		status_bar_error("Recursive rename doesn't accept list of new names");
		return 1;
	}
	if(!can_change_view_files(view))
	{
		return 0;
	}

	nfiles = 0;
	files = NULL;
	entry = NULL;
	while(iter_marked_entries(view, &entry))
	{
		char path[PATH_MAX];
		get_short_path_of(view, entry, 0, sizeof(path), path);

		if(recursive)
		{
			files = add_files_to_list(path, files, &nfiles);
		}
		else
		{
			nfiles = add_to_string_array(&files, nfiles, 1, path);
		}
	}

	is_dup = calloc(nfiles, 1);
	if(is_dup == NULL)
	{
		free_string_array(files, nfiles);
		show_error_msg("Memory Error", "Unable to allocate enough memory");
		return 0;
	}

	// add by sim1 *****************
	int bg = cfg.vi_cmd_bg;
	int bg_x = cfg.vi_x_cmd_bg;
	char *vicmd = cfg.vi_command;
	char *vicmd_x = cfg.vi_x_command;
	char *vicmd_abs = "vim";

	cfg.vi_cmd_bg = 0;
	cfg.vi_x_cmd_bg = 0;
	cfg.vi_command = vicmd_abs;
	cfg.vi_x_command = vicmd_abs;
	// add by sim1 *****************

	/* If we weren't given list of new file names, obtain it from the user. */
	if(nlines == 0)
	{
		if(nfiles == 0 || (list = edit_list(nfiles, files, &nlines, 0)) == NULL)
		{
			status_bar_message("0 files renamed");
		}
		else
		{
			free_list = 1;
		}
	}

	/* If nlines is 0 here, do nothing. */
	if(nlines != 0 && is_name_list_ok(nfiles, nlines, list, files) &&
			is_rename_list_ok(files, is_dup, nfiles, list))
	{
		const int renamed = perform_renaming(view, files, is_dup, nfiles, list);
		if(renamed >= 0)
		{
			status_bar_messagef("%d file%s renamed", renamed,
					(renamed == 1) ? "" : "s");
		}
	}

	if(free_list)
	{
		free_string_array(list, nlines);
	}
	free_string_array(files, nfiles);
	free(is_dup);

	clean_selected_files(view);
	redraw_view(view);
	curr_stats.save_msg = 1;

	// add by sim1 *****************
	cfg.vi_cmd_bg = bg;
	cfg.vi_x_cmd_bg = bg_x;
	cfg.vi_command = vicmd;
	cfg.vi_x_command = vicmd_x;
	// add by sim1 *****************

	return 1;
}

/* Renames files named files in current directory of the view to dst.  is_dup
 * marks elements that are in both lists.  Lengths of all lists must be equal to
 * len.  Returns number of renamed files. */
static int
perform_renaming(FileView *view, char *files[], char is_dup[], int len,
		char *dst[])
{
	char buf[MAX(10 + NAME_MAX, COMMAND_GROUP_INFO_LEN) + 1];
	size_t buf_len;
	int i;
	int renamed = 0;
	char **const orig_names = calloc(len, sizeof(*orig_names));
	const char *const curr_dir = flist_get_dir(view);

	buf_len = snprintf(buf, sizeof(buf), "rename in %s: ",
			replace_home_part(curr_dir));

	for(i = 0; i < len && buf_len < COMMAND_GROUP_INFO_LEN; i++)
	{
		if(buf[buf_len - 2] != ':')
		{
			strncat(buf, ", ", sizeof(buf) - buf_len - 1);
			buf_len = strlen(buf);
		}
		buf_len += snprintf(buf + buf_len, sizeof(buf) - buf_len, "%s to %s",
				files[i], dst[i]);
	}

	cmd_group_begin(buf);

	/* Stage 1: give files that are in both source and destination lists temporary
	 *          names. */
	for(i = 0; i < len; ++i)
	{
		const char *unique_name;

		if(dst[i][0] == '\0')
			continue;
		if(strcmp(dst[i], files[i]) == 0)
			continue;
		if(!is_dup[i])
			continue;

		unique_name = make_name_unique(files[i]);
		if(mv_file(files[i], curr_dir, unique_name, curr_dir, OP_MOVETMP2, 1,
					NULL) != 0)
		{
			cmd_group_end();
			if(!last_cmd_group_empty())
			{
				undo_group();
			}
			show_error_msg("Rename", "Failed to perform temporary rename");
			curr_stats.save_msg = 1;
			free_string_array(orig_names, len);
			return 0;
		}
		orig_names[i] = files[i];
		files[i] = strdup(unique_name);
	}

	/* Stage 2: rename all files (including those renamed at Stage 1) to their
	 *          final names. */
	for(i = 0; i < len; ++i)
	{
		if(dst[i][0] == '\0')
			continue;
		if(strcmp(dst[i], files[i]) == 0)
			continue;

		if(mv_file(files[i], curr_dir, dst[i], curr_dir,
				is_dup[i] ? OP_MOVETMP1 : OP_MOVE, 1, NULL) == 0)
		{
			char path[PATH_MAX];
			dir_entry_t *entry;
			const char *const old_name = is_dup[i] ? orig_names[i] : files[i];
			const char *new_name;

			++renamed;

			to_canonic_path(old_name, curr_dir, path, sizeof(path));
			entry = entry_from_path(view->dir_entry, view->list_rows, path);
			if(entry == NULL)
			{
				continue;
			}

			new_name = get_last_path_component(dst[i]);

			/* For regular views rename file in internal structures for correct
				* positioning of cursor after reloading.  For custom views rename to
				* prevent files from disappearing. */
			fentry_rename(view, entry, new_name);

			if(flist_custom_active(view))
			{
				entry = entry_from_path(view->custom.entries,
						view->custom.entry_count, path);
				if(entry != NULL)
				{
					fentry_rename(view, entry, new_name);
				}
			}
		}
	}

	cmd_group_end();

	free_string_array(orig_names, len);
	return renamed;
}

/* Checks rename correctness and forms an array of duplication marks.
 * Directory names in files array should be without trailing slash. */
TSTATIC int
is_rename_list_ok(char *files[], char is_dup[], int len, char *list[])
{
	int i;
	const char *const work_dir = flist_get_dir(curr_view);
	for(i = 0; i < len; ++i)
	{
		int j;

		const int check_result =
			check_file_rename(work_dir, files[i], list[i], ST_NONE);
		if(check_result < 0)
		{
			continue;
		}

		for(j = 0; j < len; ++j)
		{
			if(strcmp(list[i], files[j]) == 0 && !is_dup[j])
			{
				is_dup[j] = 1;
				break;
			}
		}
		if(j >= len && check_result == 0)
		{
			/* Invoke check_file_rename() again, but this time to produce error
			 * message. */
			(void)check_file_rename(work_dir, files[i], list[i], ST_STATUS_BAR);
			break;
		}
	}
	return i >= len;
}

int
incdec_names(FileView *view, int k)
{
	size_t names_len = 0;
	char **names = NULL;
	size_t tmp_len = 0;
	char **tmp_names = NULL;
	char undo_msg[COMMAND_GROUP_INFO_LEN];
	dir_entry_t *entry;
	int i;
	int err, nrenames, nrenamed;

	snprintf(undo_msg, sizeof(undo_msg), "<c-a> in %s: ",
			replace_home_part(flist_get_dir(view)));
	append_marked_files(view, undo_msg, NULL);

	entry = NULL;
	while(iter_marked_entries(view, &entry))
	{
		char full_path[PATH_MAX];

		if(strpbrk(entry->name, "0123456789") == NULL)
		{
			entry->marked = 0;
			continue;
		}

		get_full_path_of(entry, sizeof(full_path), full_path);

		names_len = add_to_string_array(&names, names_len, 1, full_path);
		tmp_len = add_to_string_array(&tmp_names, tmp_len, 1,
				make_name_unique(entry->name));
	}

	err = 0;

	entry = NULL;
	while(iter_marked_entries(view, &entry))
	{
		char new_path[PATH_MAX];
		const char *const new_fname = incdec_name(entry->name, k);

		snprintf(new_path, sizeof(new_path), "%s/%s", entry->origin, new_fname);

		/* Skip check_file_rename() for final name that matches one of original
		 * names. */
		if(is_in_string_array_os(names, names_len, new_path))
		{
			continue;
		}

		if(check_file_rename(entry->origin, entry->name, new_fname,
					ST_STATUS_BAR) != 0)
		{
			continue;
		}

		err = -1;
		break;
	}

	free_string_array(names, names_len);

	nrenames = 0;
	nrenamed = 0;

	/* Two-step renaming. */
	cmd_group_begin(undo_msg);

	entry = NULL;
	i = 0;
	while(!err && iter_marked_entries(view, &entry))
	{
		const char *const path = entry->origin;
		/* Rename: <original name> -> <temporary name>. */
		if(mv_file(entry->name, path, tmp_names[i++], path, OP_MOVETMP4, 1,
					NULL) != 0)
		{
			err = 1;
			break;
		}
		++nrenames;
	}

	entry = NULL;
	i = 0;
	while(!err && iter_marked_entries(view, &entry))
	{
		const char *const path = entry->origin;
		const char *const new_fname = incdec_name(entry->name, k);
		/* Rename: <temporary name> -> <final name>. */
		if(mv_file(tmp_names[i++], path, new_fname, path, OP_MOVETMP3, 1,
					NULL) != 0)
		{
			err = 1;
			break;
		}
		fixup_entry_after_rename(view, entry, new_fname);
		++nrenames;
		++nrenamed;
	}

	cmd_group_end();

	free_string_array(tmp_names, tmp_len);

	if(err)
	{
		if(err > 0 && !last_cmd_group_empty())
		{
			undo_group();
		}
	}

	if(nrenames > 0)
	{
		ui_view_schedule_full_reload(view);
	}

	if(err > 0)
	{
		status_bar_error("Rename error");
	}
	else if(err == 0)
	{
		status_bar_messagef("%d file%s renamed", nrenamed,
				(nrenamed == 1) ? "" : "s");
	}

	return 1;
}

/* Increments/decrements first number in fname k time, if any. Returns pointer
 * to statically allocated buffer. */
TSTATIC const char *
incdec_name(const char fname[], int k)
{
	static char result[NAME_MAX];
	char format[16];
	char *b, *e;
	int i, n;

	b = strpbrk(fname, "0123456789");
	if(b == NULL)
	{
		copy_str(result, sizeof(result), fname);
		return result;
	}

	n = 0;
	while(b[n] == '0' && isdigit(b[n + 1]))
	{
		++n;
	}

	if(b != fname && b[-1] == '-')
	{
		--b;
	}

	i = strtol(b, &e, 10);

	if(i + k < 0)
	{
		++n;
	}

	copy_str(result, b - fname + 1, fname);
	snprintf(format, sizeof(format), "%%0%dd%%s", n + count_digits(i));
	snprintf(result + (b - fname), sizeof(result) - (b - fname), format, i + k,
			e);

	return result;
}

/* Counts number of digits in passed number.  Returns the count. */
static int
count_digits(int number)
{
	int result = 0;
	while(number != 0)
	{
		number /= 10;
		result++;
	}
	return MAX(1, result);
}

/* Returns value > 0 if rename is correct, < 0 if rename isn't needed and 0
 * when rename operation should be aborted.  silent parameter controls whether
 * error dialog or status bar message should be shown, 0 means dialog. */
TSTATIC int
check_file_rename(const char dir[], const char old[], const char new[],
		SignalType signal_type)
{
	if(!is_file_name_changed(old, new))
	{
		return -1;
	}

	if(path_exists_at(dir, new, DEREF) && stroscmp(old, new) != 0 &&
			!is_case_change(old, new))
	{
		switch(signal_type)
		{
			case ST_STATUS_BAR:
				status_bar_errorf("File \"%s\" already exists", new);
				curr_stats.save_msg = 1;
				break;
			case ST_DIALOG:
				show_error_msg("File exists",
						"That file already exists. Will not overwrite.");
				break;

			default:
				assert(signal_type == ST_NONE && "Unhandled signaling type");
				break;
		}
		return 0;
	}

	return 1;
}

/* Checks whether file name change was performed.  Returns non-zero if change is
 * detected, otherwise zero is returned. */
static int
is_file_name_changed(const char old[], const char new[])
{
	/* Empty new name means reuse of the old name (rename cancellation).  Names
	 * are always compared in a case sensitive way, so that changes in case of
	 * letters triggers rename operation even for systems where paths are case
	 * insensitive. */
	return (new[0] != '\0' && strcmp(old, new) != 0);
}

#ifndef _WIN32

void
chown_files(int u, int g, uid_t uid, gid_t gid)
{
/* Integer to pointer conversion. */
#define V(e) (void *)(long)(e)

	FileView *const view = curr_view;
	char undo_msg[COMMAND_GROUP_INFO_LEN + 1];
	ops_t *ops;
	dir_entry_t *entry;
	const char *const curr_dir = flist_get_dir(view);

	ui_cancellation_reset();

	snprintf(undo_msg, sizeof(undo_msg), "ch%s in %s: ",
			((u && g) || u) ? "own" : "grp", replace_home_part(curr_dir));

	ops = get_ops(OP_CHOWN, "re-owning", curr_dir, curr_dir);

	append_marked_files(view, undo_msg, NULL);
	cmd_group_begin(undo_msg);

	entry = NULL;
	while(iter_marked_entries(view, &entry) && !ui_cancellation_requested())
	{
		char full_path[PATH_MAX];
		get_full_path_of(entry, sizeof(full_path), full_path);

		if(u && perform_operation(OP_CHOWN, ops, V(uid), full_path, NULL) == 0)
		{
			add_operation(OP_CHOWN, V(uid), V(entry->uid), full_path, "");
		}
		if(g && perform_operation(OP_CHGRP, ops, V(gid), full_path, NULL) == 0)
		{
			add_operation(OP_CHGRP, V(gid), V(entry->gid), full_path, "");
		}
	}
	cmd_group_end();

	free_ops(ops);

	ui_view_reset_selection_and_reload(view);

#undef V
}

#endif

void
change_owner(void)
{
#ifndef _WIN32
	fo_complete_cmd_func complete_func = &complete_owner;
#else
	fo_complete_cmd_func complete_func = NULL;
#endif

	mark_selection_or_current(curr_view);
	line_prompt("New owner: ", "", &change_owner_cb, complete_func, 0);
}

#ifndef _WIN32
static int
complete_owner(const char str[], void *arg)
{
	complete_user_name(str);
	return 0;
}
#endif

static void
change_owner_cb(const char new_owner[])
{
#ifndef _WIN32
	uid_t uid;

	if(is_null_or_empty(new_owner))
	{
		return;
	}

	if(get_uid(new_owner, &uid) != 0)
	{
		status_bar_errorf("Invalid user name: \"%s\"", new_owner);
		curr_stats.save_msg = 1;
		return;
	}

	chown_files(1, 0, uid, 0);
#endif
}

static void
change_group_cb(const char new_group[])
{
#ifndef _WIN32
	gid_t gid;

	if(is_null_or_empty(new_group))
	{
		return;
	}

	if(get_gid(new_group, &gid) != 0)
	{
		status_bar_errorf("Invalid group name: \"%s\"", new_group);
		curr_stats.save_msg = 1;
		return;
	}

	chown_files(0, 1, 0, gid);
#endif
}

void
change_group(void)
{
#ifndef _WIN32
	fo_complete_cmd_func complete_func = &complete_group;
#else
	fo_complete_cmd_func complete_func = NULL;
#endif

	mark_selection_or_current(curr_view);
	line_prompt("New group: ", "", &change_group_cb, complete_func, 0);
}

#ifndef _WIN32
static int
complete_group(const char str[], void *arg)
{
	complete_group_name(str);
	return 0;
}
#endif

static void
change_link_cb(const char new_target[])
{
	char undo_msg[COMMAND_GROUP_INFO_LEN];
	char full_path[PATH_MAX];
	char linkto[PATH_MAX];
	const char *fname;
	ops_t *ops;
	const char *const curr_dir = flist_get_dir(curr_view);

	if(is_null_or_empty(new_target))
	{
		return;
	}

	curr_stats.confirmed = 1;

	get_current_full_path(curr_view, sizeof(full_path), full_path);
	if(get_link_target(full_path, linkto, sizeof(linkto)) != 0)
	{
		show_error_msg("Error", "Can't read link");
		return;
	}

	ops = get_ops(OP_SYMLINK2, "re-targeting", curr_dir, curr_dir);

	fname = get_last_path_component(full_path);
	snprintf(undo_msg, sizeof(undo_msg), "cl in %s: on %s from \"%s\" to \"%s\"",
			replace_home_part(flist_get_dir(curr_view)), fname, linkto, new_target);
	cmd_group_begin(undo_msg);

	if(perform_operation(OP_REMOVESL, ops, NULL, full_path, NULL) == 0)
	{
		add_operation(OP_REMOVESL, NULL, NULL, full_path, linkto);
	}
	if(perform_operation(OP_SYMLINK2, ops, NULL, new_target, full_path) == 0)
	{
		add_operation(OP_SYMLINK2, NULL, NULL, new_target, full_path);
	}

	cmd_group_end();

	free_ops(ops);
}

int
change_link(FileView *view)
{
	char full_path[PATH_MAX];
	char linkto[PATH_MAX];
	const dir_entry_t *const entry = get_current_entry(view);

	if(!symlinks_available())
	{
		show_error_msg("Symbolic Links Error",
				"Your OS doesn't support symbolic links");
		return 0;
	}
	if(!can_change_view_files(view))
	{
		return 0;
	}

	if(entry->type != FT_LINK)
	{
		status_bar_error("File is not a symbolic link");
		return 1;
	}

	get_full_path_of(entry, sizeof(full_path), full_path);
	if(get_link_target(full_path, linkto, sizeof(linkto)) != 0)
	{
		show_error_msg("Error", "Can't read link");
		return 0;
	}

	line_prompt("Link target: ", linkto, &change_link_cb, &complete_filename, 0);
	return 0;
}

static int
complete_filename(const char str[], void *arg)
{
	const char *name_begin = after_last(str, '/');
	filename_completion(str, CT_ALL_WOE, 0);
	return name_begin - str;
}

static void
prompt_dest_name(const char *src_name)
{
	char prompt[128 + PATH_MAX];

	snprintf(prompt, ARRAY_LEN(prompt), "New name for %s: ", src_name);
	line_prompt(prompt, src_name, put_confirm_cb, NULL, 0);
}

/* Merges src into dst.  Returns zero on success, otherwise non-zero is
 * returned. */
TSTATIC int
merge_dirs(const char src[], const char dst[], ops_t *ops)
{
	struct stat st;
	DIR *dir;
	struct dirent *d;
	int result;

	if(os_stat(src, &st) != 0)
	{
		return -1;
	}

	dir = os_opendir(src);
	if(dir == NULL)
	{
		return -1;
	}

	/* Make sure target directory exists.  Ignore error as we don't care whether
	 * it existed before we try to create it and following operations will fail
	 * if we can't create this directory for some reason. */
	(void)perform_operation(OP_MKDIR, NULL, (void *)(size_t)1, dst, NULL);

	while((d = os_readdir(dir)) != NULL)
	{
		char src_path[PATH_MAX];
		char dst_path[PATH_MAX];

		if(is_builtin_dir(d->d_name))
		{
			continue;
		}

		snprintf(src_path, sizeof(src_path), "%s/%s", src, d->d_name);
		snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, d->d_name);

		if(is_dir_entry(dst_path, d))
		{
			if(merge_dirs(src_path, dst_path, ops) != 0)
			{
				break;
			}
		}
		else
		{
			if(perform_operation(OP_MOVEF, put_confirm.ops, NULL, src_path,
						dst_path) != 0)
			{
				break;
			}
			add_operation(OP_MOVEF, put_confirm.ops, NULL, src_path, dst_path);
		}
	}
	os_closedir(dir);

	if(d != NULL)
	{
		return 1;
	}

	result = perform_operation(OP_RMDIR, put_confirm.ops, NULL, src, NULL);
	if(result == 0)
	{
		add_operation(OP_RMDIR, NULL, NULL, src, "");
	}

	/* Clone file properties as the last step, because modifying directory affects
	 * timestamps and permissions can affect operations. */
	clone_timestamps(dst, src, &st);
	(void)chmod(dst, st.st_mode);

	return result;
}

static void
put_confirm_cb(const char dest_name[])
{
	if(is_null_or_empty(dest_name))
	{
		return;
	}

	if(replace_string(&put_confirm.dest_name, dest_name) != 0)
	{
		show_error_msg("Memory Error", "Unable to allocate enough memory");
		return;
	}

	if(put_next(0) == 0)
	{
		++put_confirm.index;
		curr_stats.save_msg = put_files_i(put_confirm.view, 0);
	}
}

/* Continues putting files. */
static void
put_continue(int force)
{
	if(put_next(force) == 0)
	{
		++put_confirm.index;
		curr_stats.save_msg = put_files_i(put_confirm.view, 0);
	}
}

/* Prompt user for conflict resolution strategy about given filename. */
static void
prompt_what_to_do(const char fname[])
{
	/* Strange spacing is for left alignment.  Doesn't look nice here, but it is
	 * problematic to get such alignment otherwise. */
	static const response_variant
		rename        = { .key = 'r', .descr = "[r]ename (also Enter)        \n" },
		enter         = { .key = '\r', .descr = "" },
		skip          = { .key = 's', .descr = "[s]kip " },
		skip_all      = { .key = 'S', .descr = " [S]kip all          \n" },
		append        = { .key = 'a', .descr = "[a]ppend to the end          \n" },
		overwrite     = { .key = 'o', .descr = "[o]verwrite " },
		overwrite_all = { .key = 'O', .descr = " [O]verwrite all\n" },
		merge         = { .key = 'm', .descr = "[m]erge " },
		merge_all     = { .key = 'M', .descr = " [M]erge all        \n" },
		escape        = { .key = NC_C_c, .descr = "\nEsc or Ctrl-C to cancel" };

	char msg[PATH_MAX];
	char response;
	response_variant responses[11] = {};
	size_t i = 0;

	responses[i++] = rename;
	responses[i++] = enter;
	responses[i++] = skip;
	responses[i++] = skip_all;
	if(cfg.use_system_calls && !is_dir(fname))
	{
		responses[i++] = append;
	}
	responses[i++] = overwrite;
	responses[i++] = overwrite_all;
	if(put_confirm.allow_merge)
	{
		responses[i++] = merge;
		responses[i++] = merge_all;
	}

	responses[i++] = escape;
	assert(i < ARRAY_LEN(responses) && "Array is too small.");

	/* Screen needs to be restored after displaying progress dialog. */
	modes_update();

	snprintf(msg, sizeof(msg), "Name conflict for %s.  What to do?", fname);
	response = options_prompt("File Conflict", msg, responses);
	handle_prompt_response(fname, response);
}

/* Handles response to the prompt asked by prompt_what_to_do(). */
static void
handle_prompt_response(const char fname[], char response)
{
	if(response == '\r' || response == 'r')
	{
		prompt_dest_name(fname);
	}
	else if(response == 's' || response == 'S')
	{
		if(response == 'S')
		{
			put_confirm.skip_all = 1;
		}

		++put_confirm.index;
		curr_stats.save_msg = put_files_i(put_confirm.view, 0);
	}
	else if(response == 'o')
	{
		put_continue(1);
	}
	else if(response == 'a' && cfg.use_system_calls && !is_dir(fname))
	{
		put_confirm.append = 1;
		put_continue(0);
	}
	else if(response == 'O')
	{
		put_confirm.overwrite_all = 1;
		put_continue(1);
	}
	else if(put_confirm.allow_merge && response == 'm')
	{
		put_confirm.merge = 1;
		put_continue(1);
	}
	else if(put_confirm.allow_merge && response == 'M')
	{
		put_confirm.merge_all = 1;
		put_continue(1);
	}
	else if(response != NC_C_c)
	{
		prompt_what_to_do(fname);
	}
}

int
put_files(FileView *view, int at, int reg_name, int move)
{
	const CopyMoveLikeOp op = move ? CMLO_MOVE : CMLO_COPY;
	const char *const descr = move ? "Putting" : "putting";
	return initiate_put_files(view, at, op, descr, reg_name);
}

int
put_files_bg(FileView *view, int at, int reg_name, int move)
{
	char task_desc[COMMAND_GROUP_INFO_LEN];
	size_t task_desc_len;
	int i;
	bg_args_t *args;
	reg_t *reg;
	const char *const dst_dir = get_dst_dir(view, at);

	/* Check that operation generally makes sense given our input. */

	if(!can_add_files_to_view(view, at))
	{
		return 0;
	}

	reg = regs_find(tolower(reg_name));
	if(reg == NULL || reg->nfiles < 1)
	{
		status_bar_error(reg == NULL ? "No such register" : "Register is empty");
		return 1;
	}

	/* Prepare necessary data for background procedure and perform checks to
	 * ensure there will be no conflicts. */

	args = calloc(1, sizeof(*args));
	args->move = move;
	copy_str(args->path, sizeof(args->path), dst_dir);

	task_desc_len = snprintf(task_desc, sizeof(task_desc), "%cut in %s: ",
			move ? 'P' : 'p', replace_home_part(dst_dir));
	for(i = 0; i < reg->nfiles; ++i)
	{
		char *const src = reg->files[i];
		const char *dst_name;
		char *dst;
		int j;

		chosp(src);

		if(!path_exists(src, NODEREF))
		{
			/* Skip nonexistent files. */
			continue;
		}

		append_fname(task_desc, task_desc_len, src);
		task_desc_len = strlen(task_desc);

		args->sel_list_len = add_to_string_array(&args->sel_list,
				args->sel_list_len, 1, src);

		if(is_under_trash(src))
		{
			dst_name = get_real_name_from_trash_name(src);
		}
		else
		{
			dst_name = get_last_path_component(src);
		}

		/* Check that no destination files have the same name. */
		for(j = 0; j < args->nlines; ++j)
		{
			if(stroscmp(get_last_path_component(args->list[j]), dst_name) == 0)
			{
				status_bar_errorf("Two destination files have name \"%s\"", dst_name);
				free_bg_args(args);
				return 1;
			}
		}

		dst = format_str("%s/%s", args->path, dst_name);
		args->nlines = put_into_string_array(&args->list, args->nlines, dst);

		if(!paths_are_equal(src, dst) && path_exists(dst, NODEREF))
		{
			status_bar_errorf("File \"%s\" already exists", dst);
			free_bg_args(args);
			return 1;
		}
	}

	/* Initiate the operation. */

	args->ops = get_bg_ops((args->move ? OP_MOVE : OP_COPY),
			move ? "Putting" : "putting", args->path);

	if(bg_execute(task_desc, "...", args->sel_list_len, 1, &put_files_in_bg,
				args) != 0)
	{
		free_bg_args(args);

		show_error_msg("Can't put files",
				"Failed to initiate background operation");
	}

	return 0;
}

/* Entry point for background task that puts files. */
static void
put_files_in_bg(bg_op_t *bg_op, void *arg)
{
	size_t i;
	bg_args_t *const args = arg;
	ops_t *ops = args->ops;
	bg_ops_init(ops, bg_op);

	if(ops->use_system_calls)
	{
		size_t i;
		bg_op_set_descr(bg_op, "estimating...");
		for(i = 0U; i < args->sel_list_len; ++i)
		{
			const char *const src = args->sel_list[i];
			const char *const dst = args->list[i];
			ops_enqueue(ops, src, dst);
		}
	}

	for(i = 0U; i < args->sel_list_len; ++i, ++bg_op->done)
	{
		struct stat src_st;
		const char *const src = args->sel_list[i];
		const char *const dst = args->list[i];

		if(paths_are_equal(src, dst))
		{
			/* Just ignore this file. */
			continue;
		}

		if(os_lstat(src, &src_st) != 0)
		{
			/* File isn't there, assume that it's fine and don't error in this
			 * case. */
			continue;
		}

		if(path_exists(dst, NODEREF))
		{
			/* This file wasn't here before (when checking in put_files_bg()), won't
			 * overwrite. */
			continue;
		}

		bg_op_set_descr(bg_op, src);
		(void)perform_operation(ops->main_op, ops, (void *)1, src, dst);
	}

	free_bg_args(args);
}

TSTATIC const char *
gen_clone_name(const char normal_name[])
{
	static char result[NAME_MAX];

	char extension[NAME_MAX];
	int i;
	size_t len;
	char *p;

	copy_str(result, sizeof(result), normal_name);
	chosp(result);

	copy_str(extension, sizeof(extension), cut_extension(result));

	len = strlen(result);
	i = 1;
	if(result[len - 1] == ')' && (p = strrchr(result, '(')) != NULL)
	{
		char *t;
		long l;
		if((l = strtol(p + 1, &t, 10)) > 0 && t[1] == '\0')
		{
			len = p - result;
			i = l + 1;
		}
	}

	do
	{
		snprintf(result + len, sizeof(result) - len, "(%d)%s%s", i++,
				(extension[0] == '\0') ? "" : ".", extension);
	}
	while(path_exists(result, DEREF));

	return result;
}

static int
is_clone_list_ok(int count, char **list)
{
	int i;
	for(i = 0; i < count; i++)
	{
		if(path_exists(list[i], DEREF))
		{
			status_bar_errorf("File \"%s\" already exists", list[i]);
			return 0;
		}
	}
	return 1;
}

int
clone_files(FileView *view, char *list[], int nlines, int force, int copies)
{
	int i;
	char undo_msg[COMMAND_GROUP_INFO_LEN + 1];
	char dst_path[PATH_MAX];
	char **marked;
	size_t nmarked;
	int custom_fnames;
	int nmarked_files;
	int with_dir = 0;
	int from_file;
	dir_entry_t *entry;
	ops_t *ops;
	const char *const curr_dir = flist_get_dir(view);

	if(!can_read_selected_files(view))
	{
		return 0;
	}

	if(nlines == 1)
	{
		with_dir = check_dir_path(view, list[0], dst_path, sizeof(dst_path));
		if(with_dir)
		{
			nlines = 0;
		}
	}
	else
	{
		if(!can_add_files_to_view(view, -1))
		{
			return 0;
		}

		copy_str(dst_path, sizeof(dst_path), get_dst_dir(view, -1));
	}
	if(!check_if_dir_writable(with_dir ? DR_DESTINATION : DR_CURRENT, dst_path))
	{
		return 0;
	}

	marked = grab_marked_files(view, &nmarked);

	from_file = nlines < 0;
	if(from_file)
	{
		list = edit_list(nmarked, marked, &nlines, 0);
		if(list == NULL)
		{
			free_string_array(marked, nmarked);
			return 0;
		}
	}

	free_string_array(marked, nmarked);

	if(nlines > 0 &&
			(!is_name_list_ok(nmarked, nlines, list, NULL) ||
			(!force && !is_clone_list_ok(nlines, list))))
	{
		redraw_view(view);
		if(from_file)
		{
			free_string_array(list, nlines);
		}
		return 1;
	}

	clean_selected_files(view);

	if(with_dir)
	{
		snprintf(undo_msg, sizeof(undo_msg), "clone in %s to %s: ", curr_dir,
				list[0]);
	}
	else
	{
		snprintf(undo_msg, sizeof(undo_msg), "clone in %s: ", curr_dir);
	}
	append_marked_files(view, undo_msg, list);

	ops = get_ops(OP_COPY, "Cloning", curr_dir, with_dir ? list[0] : curr_dir);

	ui_cancellation_reset();

	nmarked_files = enqueue_marked_files(ops, view, dst_path, 0);

	custom_fnames = (nlines > 0);

	cmd_group_begin(undo_msg);
	entry = NULL;
	i = 0;
	while(iter_marked_entries(view, &entry) && !ui_cancellation_requested())
	{
		int err;
		int j;
		const char *const name = entry->name;
		const char *const clone_dst = with_dir ? dst_path : entry->origin;
		const char *clone_name;
		if(custom_fnames)
		{
			clone_name = list[i];
		}
		else
		{
			clone_name = path_exists_at(clone_dst, name, DEREF)
			           ? gen_clone_name(name)
			           : name;
		}

		progress_msg("Cloning files", i, nmarked_files);

		err = 0;
		for(j = 0; j < copies; ++j)
		{
			if(path_exists_at(clone_dst, clone_name, DEREF))
			{
				clone_name = gen_clone_name(custom_fnames ? list[i] : name);
			}
			err += clone_file(entry, clone_dst, clone_name, ops);
		}

		/* Don't update cursor position if more than one file is cloned. */
		if(nmarked == 1U)
		{
			fixup_entry_after_rename(view, entry, clone_name);
		}
		ops_advance(ops, err == 0);

		++i;
	}
	cmd_group_end();

	ui_views_reload_filelists();
	if(from_file)
	{
		free_string_array(list, nlines);
	}

	status_bar_messagef("%d file%s cloned%s", ops->succeeded,
			(ops->succeeded == 1) ? "" : "s", get_cancellation_suffix());

	free_ops(ops);

	return 1;
}

/* Makes list of marked filenames.  *nmarked is always set (0 for empty list).
 * Returns pointer to the list, NULL for empty list. */
static char **
grab_marked_files(FileView *view, size_t *nmarked)
{
	char **marked = NULL;
	dir_entry_t *entry = NULL;
	*nmarked = 0;
	while(iter_marked_entries(view, &entry))
	{
		*nmarked = add_to_string_array(&marked, *nmarked, 1, entry->name);
	}
	return marked;
}

/* Clones single file/directory to directory specified by the path under name in
 * the clone.  Returns zero on success, otherwise non-zero is returned. */
static int
clone_file(const dir_entry_t *entry, const char path[], const char clone[],
		ops_t *ops)
{
	char full_path[PATH_MAX];
	char clone_name[PATH_MAX];

	to_canonic_path(clone, path, clone_name, sizeof(clone_name));
	if(path_exists(clone_name, DEREF))
	{
		if(perform_operation(OP_REMOVESL, NULL, NULL, clone_name, NULL) != 0)
		{
			return 1;
		}
	}

	get_full_path_of(entry, sizeof(full_path), full_path);

	if(perform_operation(OP_COPY, ops, NULL, full_path, clone_name) != 0)
	{
		return 1;
	}

	add_operation(OP_COPY, NULL, NULL, full_path, clone_name);
	return 0;
}

/* Uses dentry to check file type and falls back to lstat() if dentry contains
 * unknown type. */
static int
is_dir_entry(const char full_path[], const struct dirent* dentry)
{
#ifndef _WIN32
	struct stat s;
	if(dentry->d_type != DT_UNKNOWN)
	{
		return dentry->d_type == DT_DIR;
	}
	if(os_lstat(full_path, &s) == 0 && s.st_ino != 0)
	{
		return (s.st_mode&S_IFMT) == S_IFDIR;
	}
	return 0;
#else
	return is_dir(full_path);
#endif
}

int
put_links(FileView *view, int reg_name, int relative)
{
	const CopyMoveLikeOp op = relative ? CMLO_LINK_REL : CMLO_LINK_ABS;
	return initiate_put_files(view, -1, op, "Symlinking", reg_name);
}

/* Performs preparations necessary for putting files/links.  Returns new value
 * for save_msg flag. */
static int
initiate_put_files(FileView *view, int at, CopyMoveLikeOp op,
		const char descr[], int reg_name)
{
	reg_t *reg;
	int i;
	const char *const dst_dir = get_dst_dir(view, at);

	if(!can_add_files_to_view(view, -1))
	{
		return 0;
	}

	reg = regs_find(tolower(reg_name));
	if(reg == NULL || reg->nfiles < 1)
	{
		status_bar_error("Register is empty");
		return 1;
	}

	reset_put_confirm(cmlo_to_op(op), descr, dst_dir);

	put_confirm.op = op;
	put_confirm.reg = reg;
	put_confirm.view = view;

	ui_cancellation_reset();
	ui_cancellation_enable();

	for(i = 0; i < reg->nfiles && !ui_cancellation_requested(); ++i)
	{
		ops_enqueue(put_confirm.ops, reg->files[i], dst_dir);
	}

	ui_cancellation_disable();

	return put_files_i(view, 1);
}

/* Gets operation kind that corresponds to copy/move-like operation.  Returns
 * the kind. */
static OPS
cmlo_to_op(CopyMoveLikeOp op)
{
	switch(op)
	{
		case CMLO_COPY:
			return OP_COPY;
		case CMLO_MOVE:
			return OP_MOVE;
		case CMLO_LINK_REL:
		case CMLO_LINK_ABS:
			return OP_SYMLINK;

		default:
			assert(0 && "Unexpected operation type.");
			return OP_COPY;
	}
}

/* Resets state of global put_confirm variable in this module. */
static void
reset_put_confirm(OPS main_op, const char descr[], const char dst_dir[])
{
	free(put_confirm.dest_name);
	free(put_confirm.dest_dir);
	memset(&put_confirm, 0, sizeof(put_confirm));
	put_confirm.dest_dir = strdup(dst_dir);

	put_confirm.ops = get_ops(main_op, descr, dst_dir, dst_dir);
}

/* Returns new value for save_msg flag. */
static int
put_files_i(FileView *view, int start)
{
	if(start)
	{
		char undo_msg[COMMAND_GROUP_INFO_LEN + 1];
		const char *descr;
		const int from_trash = is_under_trash(put_confirm.reg->files[0]);

		if(put_confirm.op == CMLO_LINK_ABS)
		{
			descr = "put absolute links";
		}
		else if(put_confirm.op == CMLO_LINK_REL)
		{
			descr = "put relative links";
		}
		else
		{
			descr = (put_confirm.op == CMLO_MOVE || from_trash) ? "Put" : "put";
		}

		snprintf(undo_msg, sizeof(undo_msg), "%s in %s: ", descr,
				replace_home_part(flist_get_dir(view)));
		cmd_group_begin(undo_msg);
		cmd_group_end();
	}

	if(vifm_chdir(put_confirm.dest_dir) != 0)
	{
		show_error_msg("Directory Return", "Can't chdir() to current directory");
		return 1;
	}

	ui_cancellation_reset();

	while(put_confirm.index < put_confirm.reg->nfiles)
	{
		int put_result;

		update_string(&put_confirm.dest_name, NULL);

		put_result = put_next(0);
		if(put_result > 0)
		{
			/* In this case put_next() takes care of interacting with a user. */
			return 0;
		}
		else if(put_result < 0)
		{
			status_bar_messagef("%d file%s inserted%s", put_confirm.processed,
					(put_confirm.processed == 1) ? "" : "s", get_cancellation_suffix());
			return 1;
		}
		++put_confirm.index;
	}

	regs_pack(put_confirm.reg->name);

	status_bar_messagef("%d file%s inserted%s", put_confirm.processed,
			(put_confirm.processed == 1) ? "" : "s", get_cancellation_suffix());

	free_ops(put_confirm.ops);
	ui_view_schedule_reload(put_confirm.view);

	return 1;
}

/* The force argument enables overwriting/replacing/merging.  Returns 0 on
 * success, otherwise non-zero is returned. */
static int
put_next(int force)
{
	char *filename;
	const char *dest_name;
	const char *dst_dir = put_confirm.dest_dir;
	struct stat src_st;
	char src_buf[PATH_MAX], dst_buf[PATH_MAX];
	int from_trash;
	int op;
	int move;
	int success;
	int merge;

	/* TODO: refactor this function (put_next()) */

	if(ui_cancellation_requested())
	{
		return -1;
	}

	force = force || put_confirm.overwrite_all || put_confirm.merge_all;
	merge = put_confirm.merge || put_confirm.merge_all;

	filename = put_confirm.reg->files[put_confirm.index];
	chosp(filename);
	if(os_lstat(filename, &src_st) != 0)
	{
		/* File isn't there, assume that it's fine and don't error in this case. */
		return 0;
	}

	from_trash = is_under_trash(filename);
	move = from_trash || put_confirm.op == CMLO_MOVE;

	copy_str(src_buf, sizeof(src_buf), filename);

	dest_name = put_confirm.dest_name;
	if(dest_name == NULL)
	{
		if(from_trash)
		{
			dest_name = get_real_name_from_trash_name(src_buf);
		}
		else
		{
			dest_name = find_slashr(src_buf) + 1;
		}
	}

	snprintf(dst_buf, sizeof(dst_buf), "%s/%s", dst_dir, dest_name);
	chosp(dst_buf);

	if(!put_confirm.append && path_exists(dst_buf, DEREF))
	{
		if(force)
		{
			struct stat dst_st;

			if(paths_are_equal(src_buf, dst_buf))
			{
				/* Skip if destination matches source. */
				return 0;
			}

			if(os_lstat(dst_buf, &dst_st) == 0 && (!merge ||
					S_ISDIR(dst_st.st_mode) != S_ISDIR(src_st.st_mode)))
			{
				if(perform_operation(OP_REMOVESL, put_confirm.ops, NULL, dst_buf,
							NULL) != 0)
				{
					return 0;
				}

				/* Schedule view update to reflect changes in UI. */
				ui_view_schedule_reload(put_confirm.view);
			}
			else if(!cfg.use_system_calls && get_env_type() == ET_UNIX)
			{
				remove_last_path_component(dst_buf);
			}
		}
		else if(put_confirm.skip_all)
		{
			return 0;
		}
		else
		{
			struct stat dst_st;
			put_confirm.allow_merge = os_lstat(dst_buf, &dst_st) == 0 &&
					S_ISDIR(dst_st.st_mode) && S_ISDIR(src_st.st_mode);
			prompt_what_to_do(dest_name);
			return 1;
		}
	}

	if(put_confirm.op == CMLO_LINK_REL || put_confirm.op == CMLO_LINK_ABS)
	{
		op = OP_SYMLINK;
		if(put_confirm.op == CMLO_LINK_REL)
		{
			copy_str(src_buf, sizeof(src_buf), make_rel_path(filename, dst_dir));
		}
	}
	else if(put_confirm.append)
	{
		op = move ? OP_MOVEA : OP_COPYA;
		put_confirm.append = 0;
	}
	else if(move)
	{
		op = merge ? OP_MOVEF : OP_MOVE;
	}
	else
	{
		op = merge ? OP_COPYF : OP_COPY;
	}

	progress_msg("Putting files", put_confirm.index, put_confirm.reg->nfiles);

	/* Merging directory on move requires special handling as it can't be done by
	 * move operation itself. */
	if(move && merge)
	{
		char dst_path[PATH_MAX];

		success = 1;

		cmd_group_continue();

		snprintf(dst_path, sizeof(dst_path), "%s/%s", dst_dir, dest_name);

		if(merge_dirs(src_buf, dst_path, put_confirm.ops) != 0)
		{
			success = 0;
		}

		cmd_group_end();
	}
	else
	{
		success = (perform_operation(op, put_confirm.ops, NULL, src_buf,
					dst_buf) == 0);
	}

	ops_advance(put_confirm.ops, success);

	if(success)
	{
		char *msg, *p;
		size_t len;

		/* For some reason "mv" sometimes returns 0 on cancellation. */
		if(!path_exists(dst_buf, DEREF))
		{
			return -1;
		}

		cmd_group_continue();

		msg = replace_group_msg(NULL);
		len = strlen(msg);
		p = realloc(msg, COMMAND_GROUP_INFO_LEN);
		if(p == NULL)
			len = COMMAND_GROUP_INFO_LEN;
		else
			msg = p;

		snprintf(msg + len, COMMAND_GROUP_INFO_LEN - len, "%s%s",
				(msg[len - 2] != ':') ? ", " : "", dest_name);
		replace_group_msg(msg);
		free(msg);

		if(!(move && merge))
		{
			add_operation(op, NULL, NULL, src_buf, dst_buf);
		}

		cmd_group_end();
		++put_confirm.processed;
		if(move)
		{
			update_string(&put_confirm.reg->files[put_confirm.index], NULL);
		}
	}

	return 0;
}

/* off can be NULL */
static const char *
substitute_regexp(const char *src, const char *sub, const regmatch_t *matches,
		int *off)
{
	static char buf[NAME_MAX];
	char *dst = buf;
	int i;

	for(i = 0; i < matches[0].rm_so; i++)
		*dst++ = src[i];

	while(*sub != '\0')
	{
		if(*sub == '\\')
		{
			if(sub[1] == '\0')
				break;
			else if(isdigit(sub[1]))
			{
				int n = sub[1] - '0';
				for(i = matches[n].rm_so; i < matches[n].rm_eo; i++)
					*dst++ = src[i];
				sub += 2;
				continue;
			}
			else
				sub++;
		}
		*dst++ = *sub++;
	}
	if(off != NULL)
		*off = dst - buf;

	for(i = matches[0].rm_eo; src[i] != '\0'; i++)
		*dst++ = src[i];

	*dst = '\0';

	return buf;
}

/* Returns pointer to a statically allocated buffer. */
static const char *
gsubstitute_regexp(regex_t *re, const char src[], const char sub[],
		regmatch_t matches[])
{
	static char buf[NAME_MAX];
	int off = 0;

	copy_str(buf, sizeof(buf), src);
	do
	{
		int i;
		for(i = 0; i < 10; i++)
		{
			matches[i].rm_so += off;
			matches[i].rm_eo += off;
		}

		src = substitute_regexp(buf, sub, matches, &off);
		copy_str(buf, sizeof(buf), src);

		if(matches[0].rm_eo == matches[0].rm_so)
			break;
	}
	while(regexec(re, buf + off, 10, matches, 0) == 0);

	return buf;
}

const char *
substitute_in_name(const char name[], const char pattern[], const char sub[],
		int glob)
{
	static char buf[PATH_MAX];
	regex_t re;
	regmatch_t matches[10];
	const char *dst;

	copy_str(buf, sizeof(buf), name);

	if(regcomp(&re, pattern, REG_EXTENDED) != 0)
	{
		regfree(&re);
		return buf;
	}

	if(regexec(&re, name, ARRAY_LEN(matches), matches, 0) != 0)
	{
		regfree(&re);
		return buf;
	}

	if(glob && pattern[0] != '^')
		dst = gsubstitute_regexp(&re, name, sub, matches);
	else
		dst = substitute_regexp(name, sub, matches, NULL);
	copy_str(buf, sizeof(buf), dst);

	regfree(&re);
	return buf;
}

int
substitute_in_names(FileView *view, const char pattern[], const char sub[],
		int ic, int glob)
{
	regex_t re;
	char **dest;
	int ndest;
	int cflags;
	dir_entry_t *entry;
	int err, save_msg;

	if(!can_change_view_files(view))
	{
		return 0;
	}

	if(ic == 0)
	{
		cflags = get_regexp_cflags(pattern);
	}
	else if(ic > 0)
	{
		cflags = REG_EXTENDED | REG_ICASE;
	}
	else
	{
		cflags = REG_EXTENDED;
	}

	if((err = regcomp(&re, pattern, cflags)) != 0)
	{
		status_bar_errorf("Regexp error: %s", get_regexp_error(err, &re));
		regfree(&re);
		return 1;
	}

	entry = NULL;
	ndest = 0;
	dest = NULL;
	err = 0;
	while(iter_marked_entries(view, &entry) && !err)
	{
		const char *new_fname;
		regmatch_t matches[10];
		RenameAction action;

		if(regexec(&re, entry->name, ARRAY_LEN(matches), matches, 0) != 0)
		{
			entry->marked = 0;
			continue;
		}

		if(glob)
		{
			new_fname = gsubstitute_regexp(&re, entry->name, sub, matches);
		}
		else
		{
			new_fname = substitute_regexp(entry->name, sub, matches, NULL);
		}

		action = check_rename(entry->name, new_fname, dest, ndest);
		switch(action)
		{
			case RA_SKIP:
				entry->marked = 0;
				continue;
			case RA_FAIL:
				err = 1;
				break;
			case RA_RENAME:
				ndest = add_to_string_array(&dest, ndest, 1, new_fname);
				break;

			default:
				assert(0 && "Unhandled rename action.");
				break;
		}
	}

	regfree(&re);

	if(err)
	{
		save_msg = 1;
	}
	else
	{
		save_msg = rename_marked(view, "s", pattern, sub, dest);
	}

	free_string_array(dest, ndest);

	return save_msg;
}

static const char *
substitute_tr(const char *name, const char *pattern, const char *sub)
{
	static char buf[NAME_MAX];
	char *p = buf;
	while(*name != '\0')
	{
		const char *t = strchr(pattern, *name);
		if(t != NULL)
			*p++ = sub[t - pattern];
		else
			*p++ = *name;
		name++;
	}
	*p = '\0';
	return buf;
}

int
tr_in_names(FileView *view, const char from[], const char to[])
{
	char **dest;
	int ndest;
	dir_entry_t *entry;
	int err, save_msg;

	assert(strlen(from) == strlen(to) && "Lengths don't match.");

	if(!can_change_view_files(view))
	{
		return 0;
	}

	entry = NULL;
	ndest = 0;
	dest = NULL;
	err = 0;
	while(iter_marked_entries(view, &entry) && !err)
	{
		const char *new_fname;
		RenameAction action;

		new_fname = substitute_tr(entry->name, from, to);

		action = check_rename(entry->name, new_fname, dest, ndest);
		switch(action)
		{
			case RA_SKIP:
				entry->marked = 0;
				continue;
			case RA_FAIL:
				err = 1;
				break;
			case RA_RENAME:
				ndest = add_to_string_array(&dest, ndest, 1, new_fname);
				break;

			default:
				assert(0 && "Unhandled rename action.");
				break;
		}
	}

	if(err)
	{
		save_msg = 1;
	}
	else
	{
		save_msg = rename_marked(view, "t", from, to, dest);
	}

	free_string_array(dest, ndest);

	return save_msg;
}

/* Evaluates possibility of renaming old_fname to new_fname.  Returns
 * resolution. */
static RenameAction
check_rename(const char old_fname[], const char new_fname[], char **dest,
		int ndest)
{
	/* Compare case sensitive strings even on Windows to let user rename file
	 * changing only case of some characters. */
	if(strcmp(old_fname, new_fname) == 0)
	{
		return RA_SKIP;
	}

	if(is_in_string_array(dest, ndest, new_fname))
	{
		status_bar_errorf("Name \"%s\" duplicates", new_fname);
		return RA_FAIL;
	}
	if(new_fname[0] == '\0')
	{
		status_bar_errorf("Destination name of \"%s\" is empty", old_fname);
		return RA_FAIL;
	}
	if(contains_slash(new_fname))
	{
		status_bar_errorf("Destination name \"%s\" contains slash", new_fname);
		return RA_FAIL;
	}
	if(path_exists(new_fname, NODEREF))
	{
		status_bar_errorf("File \"%s\" already exists", new_fname);
		return RA_FAIL;
	}

	return RA_RENAME;
}

int
change_case(FileView *view, int to_upper)
{
	char **dest;
	int ndest;
	dir_entry_t *entry;
	int save_msg;
	int err;

	if(!can_change_view_files(view))
	{
		return 0;
	}

	entry = NULL;
	ndest = 0;
	dest = NULL;
	err = 0;
	while(iter_marked_entries(view, &entry))
	{
		const char *const old_fname = entry->name;
		char new_fname[NAME_MAX];

		/* Ignore too small buffer errors by not caring about part that didn't
		 * fit. */
		if(to_upper)
		{
			(void)str_to_upper(old_fname, new_fname, sizeof(new_fname));
		}
		else
		{
			(void)str_to_lower(old_fname, new_fname, sizeof(new_fname));
		}

		if(strcmp(new_fname, old_fname) == 0)
		{
			entry->marked = 0;
			continue;
		}

		if(is_in_string_array(dest, ndest, new_fname))
		{
			status_bar_errorf("Name \"%s\" duplicates", new_fname);
			err = 1;
			break;
		}
		if(path_exists(new_fname, NODEREF) && !is_case_change(new_fname, old_fname))
		{
			status_bar_errorf("File \"%s\" already exists", new_fname);
			err = 1;
			break;
		}

		ndest = add_to_string_array(&dest, ndest, 1, new_fname);
	}

	if(err)
	{
		save_msg = 1;
	}
	else
	{
		save_msg = rename_marked(view, to_upper ? "gU" : "gu", NULL, NULL, dest);
	}

	free_string_array(dest, ndest);

	return save_msg;
}

/* Renames marked files using corresponding entries of the dest array.  lhs and
 * rhs can be NULL to omit their printing (both at the same time).  Returns new
 * value for save_msg flag. */
static int
rename_marked(FileView *view, const char desc[], const char lhs[],
		const char rhs[], char **dest)
{
	int i;
	int nrenamed;
	char undo_msg[COMMAND_GROUP_INFO_LEN + 1];
	dir_entry_t *entry;

	if(lhs == NULL && rhs == NULL)
	{
		snprintf(undo_msg, sizeof(undo_msg), "%s in %s: ", desc,
				replace_home_part(flist_get_dir(view)));
	}
	else
	{
		snprintf(undo_msg, sizeof(undo_msg), "%s/%s/%s/ in %s: ", desc, lhs, rhs,
				replace_home_part(flist_get_dir(view)));
	}
	append_marked_files(view, undo_msg, NULL);
	cmd_group_begin(undo_msg);

	nrenamed = 0;
	i = 0;
	entry = NULL;
	while(iter_marked_entries(view, &entry))
	{
		const char *const new_fname = dest[i++];
		if(mv_file(entry->name, entry->origin, new_fname, entry->origin, OP_MOVE, 1,
					NULL) == 0)
		{
			fixup_entry_after_rename(view, entry, new_fname);
			++nrenamed;
		}
	}

	cmd_group_end();
	status_bar_messagef("%d file%s renamed", nrenamed,
			(nrenamed == 1) ? "" : "s");

	return 1;
}

/* Updates renamed entry name when it makes sense.  This is basically to allow
 * correct cursor positioning on view reload or correct custom view update. */
static void
fixup_entry_after_rename(FileView *view, dir_entry_t *entry,
		const char new_fname[])
{
	if(entry_to_pos(view, entry) == view->list_pos || flist_custom_active(view))
	{
		fentry_rename(view, entry, new_fname);
	}
}

static int
is_copy_list_ok(const char *dst, int count, char **list)
{
	int i;
	for(i = 0; i < count; i++)
	{
		if(path_exists_at(dst, list[i], DEREF))
		{
			status_bar_errorf("File \"%s\" already exists", list[i]);
			return 0;
		}
	}
	return 1;
}

int
cpmv_files(FileView *view, char **list, int nlines, CopyMoveLikeOp op,
		int force)
{
	int err;
	int nmarked_files;
	int custom_fnames;
	int i;
	char undo_msg[COMMAND_GROUP_INFO_LEN + 1];
	dir_entry_t *entry;
	char path[PATH_MAX];
	int from_file;
	ops_t *ops;

	if((op == CMLO_LINK_REL || op == CMLO_LINK_ABS) && !symlinks_available())
	{
		show_error_msg("Symbolic Links Error",
				"Your OS doesn't support symbolic links");
		return 0;
	}

	err = cpmv_prepare(view, &list, &nlines, op, force, undo_msg,
			sizeof(undo_msg), path, sizeof(path), &from_file);
	if(err != 0)
	{
		return err > 0;
	}

	if(pane_in_dir(curr_view, path) && force)
	{
		show_error_msg("Operation Error",
				"Forcing overwrite when destination and source is same directory will "
				"lead to losing data");
		return 0;
	}

	switch(op)
	{
		case CMLO_COPY:
			ops = get_ops(OP_COPY, "Copying", flist_get_dir(view), path);
			break;
		case CMLO_MOVE:
			ops = get_ops(OP_MOVE, "Moving", flist_get_dir(view), path);
			break;
		case CMLO_LINK_REL:
		case CMLO_LINK_ABS:
			ops = get_ops(OP_SYMLINK, "Linking", flist_get_dir(view), path);
			break;

		default:
			assert(0 && "Unexpected operation type.");
			return 0;
	}

	ui_cancellation_reset();

	nmarked_files = enqueue_marked_files(ops, view, path, 0);

	cmd_group_begin(undo_msg);
	i = 0;
	entry = NULL;
	custom_fnames = (nlines > 0);
	while(iter_marked_entries(view, &entry) && !ui_cancellation_requested())
	{
		/* Must be at this level as dst might point into this buffer. */
		char src_full[PATH_MAX];

		char dst_full[PATH_MAX];
		const char *dst = custom_fnames ? list[i] : entry->name;
		int err, from_trash;

		get_full_path_of(entry, sizeof(src_full), src_full);
		from_trash = is_under_trash(src_full);

		if(from_trash && !custom_fnames)
		{
			snprintf(src_full, sizeof(src_full), "%s/%s", entry->origin, dst);
			chosp(src_full);
			dst = get_real_name_from_trash_name(src_full);
		}

		snprintf(dst_full, sizeof(dst_full), "%s/%s", path, dst);
		if(path_exists(dst_full, DEREF) && !from_trash)
		{
			(void)perform_operation(OP_REMOVESL, NULL, NULL, dst_full, NULL);
		}

		if(op == CMLO_COPY)
		{
			progress_msg("Copying files", i, nmarked_files);
		}
		else if(op == CMLO_MOVE)
		{
			progress_msg("Moving files", i, nmarked_files);
		}

		if(op == CMLO_MOVE)
		{
			err = mv_file(entry->name, entry->origin, dst, path, OP_MOVE, 1, ops);
			if(err != 0)
			{
				view->list_pos = find_file_pos_in_list(view, entry->name);
			}
		}
		else
		{
			err = cp_file(entry->origin, path, entry->name, dst, op, 1, ops);
		}

		ops_advance(ops, err == 0);

		++i;
	}
	cmd_group_end();

	ui_views_reload_filelists();
	if(from_file)
	{
		free_string_array(list, nlines);
	}

	status_bar_messagef("%d file%s successfully processed%s", ops->succeeded,
			(ops->succeeded == 1) ? "" : "s", get_cancellation_suffix());

	free_ops(ops);

	return 1;
}

/* Adds marked files to the ops.  Considers UI cancellation.  Returns number of
 * files enqueued. */
static int
enqueue_marked_files(ops_t *ops, FileView *view, const char dst_hint[],
		int to_trash)
{
	int nmarked_files = 0;
	dir_entry_t *entry = NULL;

	ui_cancellation_enable();

	while(iter_marked_entries(view, &entry) && !ui_cancellation_requested())
	{
		char full_path[PATH_MAX];

		get_full_path_of(entry, sizeof(full_path), full_path);

		if(to_trash)
		{
			char *const trash_dir = pick_trash_dir(entry->origin);
			ops_enqueue(ops, full_path, trash_dir);
			free(trash_dir);
		}
		else
		{
			ops_enqueue(ops, full_path, dst_hint);
		}

		++nmarked_files;
	}

	ui_cancellation_disable();

	return nmarked_files;
}

/* Allocates opt_t structure and configures it as needed.  Returns pointer to
 * newly allocated structure, which should be freed by free_ops(). */
static ops_t *
get_ops(OPS main_op, const char descr[], const char base_dir[],
		const char target_dir[])
{
	ops_t *const ops = ops_alloc(main_op, 0, descr, base_dir, target_dir);
	if(ops->use_system_calls)
	{
		ops->estim = ioeta_alloc(alloc_progress_data(0, ops));
	}
	return ops;
}

/* Displays simple operation progress message.  The ready is zero based. */
static void
progress_msg(const char text[], int ready, int total)
{
	if(!cfg.use_system_calls)
	{
		char msg[strlen(text) + 32];

		sprintf(msg, "%s %d/%d", text, ready + 1, total);
		show_progress(msg, 1);
		curr_stats.save_msg = 2;
	}
}

int
cpmv_files_bg(FileView *view, char **list, int nlines, int move, int force)
{
	int err;
	size_t i;
	char task_desc[COMMAND_GROUP_INFO_LEN];
	bg_args_t *args = calloc(1, sizeof(*args));

	args->nlines = nlines;
	args->move = move;
	args->force = force;

	err = cpmv_prepare(view, &list, &args->nlines, move ? CMLO_MOVE : CMLO_COPY,
			force, task_desc, sizeof(task_desc), args->path, sizeof(args->path),
			&args->from_file);
	if(err != 0)
	{
		free_bg_args(args);
		return err > 0;
	}

	args->list = args->from_file ? list : copy_string_array(list, nlines);

	general_prepare_for_bg_task(view, args);

	args->is_in_trash = malloc(args->sel_list_len);
	for(i = 0U; i < args->sel_list_len; ++i)
	{
		args->is_in_trash[i] = is_under_trash(args->sel_list[i]);
	}

	if(args->list == NULL)
	{
		int i;
		args->nlines = args->sel_list_len;
		args->list = reallocarray(NULL, args->nlines, sizeof(*args->list));
		for(i = 0; i < args->nlines; ++i)
		{
			args->list[i] = args->is_in_trash[i]
			              ? strdup(get_real_name_from_trash_name(args->sel_list[i]))
			              : strdup(get_last_path_component(args->sel_list[i]));
		}
	}

	args->ops = get_bg_ops(move ? OP_MOVE : OP_COPY, move ? "moving" : "copying",
			args->path);

	if(bg_execute(task_desc, "...", args->sel_list_len, 1, &cpmv_files_in_bg,
				args) != 0)
	{
		free_bg_args(args);

		show_error_msg("Can't process files",
				"Failed to initiate background operation");
	}

	return 0;
}

/* Performs general preparations for file copy/move-like operations: resolving
 * destination path, validating names, checking for conflicts, formatting undo
 * message.  Returns zero on success, otherwise positive number for status bar
 * message and negative number for other errors. */
static int
cpmv_prepare(FileView *view, char ***list, int *nlines, CopyMoveLikeOp op,
		int force, char undo_msg[], size_t undo_msg_len, char dst_path[],
		size_t dst_path_len, int *from_file)
{
	char **marked;
	size_t nmarked;
	int error = 0;

	if(op == CMLO_MOVE)
	{
		if(!can_change_view_files(view))
		{
			return -1;
		}
	}
	else if(op == CMLO_COPY && !can_read_selected_files(view))
	{
		return -1;
	}

	if(*nlines == 1)
	{
		if(check_dir_path(other_view, (*list)[0], dst_path, dst_path_len))
		{
			*nlines = 0;
		}
	}
	else
	{
		copy_str(dst_path, dst_path_len, get_dst_dir(other_view, -1));
	}

	if(!check_if_dir_writable(DR_DESTINATION, dst_path))
	{
		return -1;
	}

	marked = grab_marked_files(view, &nmarked);

	*from_file = *nlines < 0;
	if(*from_file)
	{
		*list = edit_list(nmarked, marked, nlines, 1);
		if(*list == NULL)
		{
			free_string_array(marked, nmarked);
			return -1;
		}
	}

	if(*nlines > 0 &&
			(!is_name_list_ok(nmarked, *nlines, *list, NULL) ||
			(!is_copy_list_ok(dst_path, *nlines, *list) && !force)))
	{
		error = 1;
	}
	if(*nlines == 0 && !force && !is_copy_list_ok(dst_path, nmarked, marked))
	{
		error = 1;
	}

	/* Custom views can contain several files with the same name. */
	if(flist_custom_active(view))
	{
		size_t i;
		for(i = 0U; i < nmarked && !error; ++i)
		{
			if(is_in_string_array(marked, i, marked[i]))
			{
				status_bar_errorf("Source name \"%s\" duplicates", marked[i]);
				curr_stats.save_msg = 1;
				error = 1;
			}
		}
	}

	free_string_array(marked, nmarked);

	if(error)
	{
		redraw_view(view);
		if(*from_file)
		{
			free_string_array(*list, *nlines);
		}
		return 1;
	}

	snprintf(undo_msg, undo_msg_len, "%s from %s to ", cmlo_to_str(op),
			replace_home_part(flist_get_dir(view)));
	snprintf(undo_msg + strlen(undo_msg), undo_msg_len - strlen(undo_msg),
			"%s: ", replace_home_part(dst_path));
	append_marked_files(view, undo_msg, (*nlines > 0) ? *list : NULL);

	if(op == CMLO_MOVE)
	{
		move_cursor_out_of(view, FLS_SELECTION);
	}

	return 0;
}

/* Checks that all selected files can be read.  Returns non-zero if so,
 * otherwise zero is returned. */
static int
can_read_selected_files(FileView *view)
{
	dir_entry_t *entry;

	if(is_unc_path(view->curr_dir))
	{
		return 1;
	}

	entry = NULL;
	while(iter_selected_entries(view, &entry))
	{
		char full_path[PATH_MAX];

		get_full_path_of(entry, sizeof(full_path), full_path);
		if(os_access(full_path, R_OK) == 0)
		{
			continue;
		}

		show_error_msgf("Access denied",
				"You don't have read permissions on \"%s\"", full_path);
		clean_selected_files(view);
		redraw_view(view);
		return 0;
	}
	return 1;
}

/* Checks path argument and resolves target directory either to the argument or
 * current directory of the view.  Returns non-zero if value of the path was
 * used, otherwise zero is returned. */
static int
check_dir_path(const FileView *view, const char path[], char buf[],
		size_t buf_len)
{
	if(path[0] == '/' || path[0] == '~')
	{
		char *const expanded_path = expand_tilde(path);
		copy_str(buf, buf_len, expanded_path);
		free(expanded_path);
	}
	else
	{
		snprintf(buf, buf_len, "%s/%s", get_dst_dir(view, -1), path);
	}

	if(is_dir(buf))
	{
		return 1;
	}

	copy_str(buf, buf_len, get_dst_dir(view, -1));
	return 0;
}

/* Prompts user with a file containing lines from orig array of length count and
 * returns modified list of strings of length *nlines or NULL on error.  The
 * ignore_change parameter makes function think that file is always changed. */
static char **
edit_list(size_t count, char **orig, int *nlines, int ignore_change)
{
	char rename_file[PATH_MAX];
	char **list = NULL;

	generate_tmp_file_name("vifm.rename", rename_file, sizeof(rename_file));

	if(write_file_of_lines(rename_file, orig, count) != 0)
	{
		show_error_msgf("Error Getting List Of Renames",
				"Can't create temporary file \"%s\": %s", rename_file, strerror(errno));
		return NULL;
	}

	if(edit_file(rename_file, ignore_change) > 0)
	{
		list = read_file_of_lines(rename_file, nlines);
		if(list == NULL)
		{
			show_error_msgf("Error Getting List Of Renames",
					"Can't open temporary file \"%s\": %s", rename_file, strerror(errno));
		}
	}

	unlink(rename_file);
	return list;
}

/* Edits the filepath in the editor checking whether it was changed.  Returns
 * negative value on error, zero when no changes were detected and positive
 * number otherwise. */
static int
edit_file(const char filepath[], int force_changed)
{
	struct stat st_before, st_after;

	if(!force_changed && os_stat(filepath, &st_before) != 0)
	{
		show_error_msgf("Error Editing File",
				"Could not stat file \"%s\" before edit: %s", filepath,
				strerror(errno));
		return -1;
	}

	if(vim_view_file(filepath, -1, -1, 0) != 0)
	{
		show_error_msgf("Error Editing File", "Editing of file \"%s\" failed.",
				filepath);
		return -1;
	}

	if(!force_changed && os_stat(filepath, &st_after) != 0)
	{
		show_error_msgf("Error Editing File",
				"Could not stat file \"%s\" after edit: %s", filepath, strerror(errno));
		return -1;
	}

	return force_changed || memcmp(&st_after.st_mtime, &st_before.st_mtime,
			sizeof(st_after.st_mtime)) != 0;
}

/* Gets string representation of a copy/move-like operation.  Returns the
 * string. */
static const char *
cmlo_to_str(CopyMoveLikeOp op)
{
	switch(op)
	{
		case CMLO_COPY:
			return "copy";
		case CMLO_MOVE:
			return "move";
		case CMLO_LINK_REL:
			return "rlink";
		case CMLO_LINK_ABS:
			return "alink";

		default:
			assert(0 && "Unexpected operation type.");
			return "";
	}
}

/* Entry point for a background task that copies/moves files. */
static void
cpmv_files_in_bg(bg_op_t *bg_op, void *arg)
{
	size_t i;
	bg_args_t *const args = arg;
	ops_t *ops = args->ops;
	bg_ops_init(ops, bg_op);

	if(ops->use_system_calls)
	{
		size_t i;
		bg_op_set_descr(bg_op, "estimating...");
		for(i = 0U; i < args->sel_list_len; ++i)
		{
			const char *const src = args->sel_list[i];
			const char *const dst = args->list[i];
			ops_enqueue(ops, src, dst);
		}
	}

	for(i = 0U; i < args->sel_list_len; ++i)
	{
		const char *const src = args->sel_list[i];
		const char *const dst = args->list[i];
		bg_op_set_descr(bg_op, src);
		cpmv_file_in_bg(ops, src, dst, args->move, args->force,
				args->is_in_trash[i], args->path);
		++bg_op->done;
	}

	free_bg_args(args);
}

/* Finishes initialization of ops for background processes. */
static void
bg_ops_init(ops_t *ops, bg_op_t *bg_op)
{
	if(ops->estim != NULL)
	{
		progress_data_t *const pdata = ops->estim->param;
		pdata->bg_op = bg_op;
	}
}

/* Allocates opt_t structure and configures it as needed.  Returns pointer to
 * newly allocated structure, which should be freed by free_ops(). */
static ops_t *
get_bg_ops(OPS main_op, const char descr[], const char dir[])
{
	ops_t *const ops = ops_alloc(main_op, 1, descr, dir, dir);
	if(ops->use_system_calls)
	{
		progress_data_t *const pdata = alloc_progress_data(1, NULL);
		ops->estim = ioeta_alloc(pdata);
	}
	return ops;
}

/* Allocates progress data with specified parameters and initializes all the
 * rest of structure fields with default values. */
static progress_data_t *
alloc_progress_data(int bg, void *info)
{
	progress_data_t *const pdata = malloc(sizeof(*pdata));

	pdata->bg = bg;
	pdata->ops = info;

	pdata->last_progress = -1;
	pdata->last_stage = (IoPs)-1;
	pdata->dialog = 0;
	pdata->width = 0;

	return pdata;
}

/* Frees ops structure previously obtained by call to get_ops().  ops can be
 * NULL. */
static void
free_ops(ops_t *ops)
{
	if(ops == NULL)
	{
		return;
	}

	if(ops->use_system_calls)
	{
		progress_data_t *const pdata = ops->estim->param;

		if(!pdata->bg && ops->errors != NULL)
		{
			char *const title = format_str("Encountered errors on %s",
					ops_describe(ops));
			show_error_msg(title, ops->errors);
			free(title);
		}

		free(ops->estim->param);
	}
	ops_free(ops);
}

/* Actual implementation of background file copying/moving. */
static void
cpmv_file_in_bg(ops_t *ops, const char src[], const char dst[], int move,
		int force, int from_trash, const char dst_dir[])
{
	char dst_full[PATH_MAX];
	snprintf(dst_full, sizeof(dst_full), "%s/%s", dst_dir, dst);
	if(path_exists(dst_full, DEREF) && !from_trash)
	{
		(void)perform_operation(OP_REMOVESL, NULL, (void *)1, dst_full, NULL);
	}

	if(move)
	{
		(void)mv_file_f(src, dst_full, OP_MOVE, 1, 0, ops);
	}
	else
	{
		(void)cp_file_f(src, dst_full, CMLO_COPY, 1, 0, ops);
	}
}

/* Adapter for mv_file_f() that accepts paths broken into directory/file
 * parts. */
static int
mv_file(const char src[], const char src_dir[], const char dst[],
		const char dst_dir[], OPS op, int cancellable, ops_t *ops)
{
	char full_src[PATH_MAX], full_dst[PATH_MAX];

	to_canonic_path(src, src_dir, full_src, sizeof(full_src));
	to_canonic_path(dst, dst_dir, full_dst, sizeof(full_dst));

	return mv_file_f(full_src, full_dst, op, 0, cancellable, ops);
}

/* Moves file from one location to another.  Returns zero on success, otherwise
 * non-zero is returned. */
static int
mv_file_f(const char src[], const char dst[], OPS op, int bg, int cancellable,
		ops_t *ops)
{
	int result;

	/* Compare case sensitive strings even on Windows to let user rename file
	 * changing only case of some characters. */
	if(strcmp(src, dst) == 0)
	{
		return 0;
	}

	result = perform_operation(op, ops, cancellable ? NULL : (void *)1, src, dst);
	if(result == 0 && !bg)
	{
		add_operation(op, NULL, NULL, src, dst);
	}
	return result;
}

/* Adapter for cp_file_f() that accepts paths broken into directory/file
 * parts. */
static int
cp_file(const char src_dir[], const char dst_dir[], const char src[],
		const char dst[], CopyMoveLikeOp op, int cancellable, ops_t *ops)
{
	char full_src[PATH_MAX], full_dst[PATH_MAX];

	to_canonic_path(src, src_dir, full_src, sizeof(full_src));
	to_canonic_path(dst, dst_dir, full_dst, sizeof(full_dst));

	return cp_file_f(full_src, full_dst, op, 0, cancellable, ops);
}

/* Copies file from one location to another.  Returns zero on success, otherwise
 * non-zero is returned. */
static int
cp_file_f(const char src[], const char dst[], CopyMoveLikeOp op, int bg,
		int cancellable, ops_t *ops)
{
	char rel_path[PATH_MAX];

	int file_op;
	int result;

	if(strcmp(src, dst) == 0)
	{
		return 0;
	}

	if(op == CMLO_COPY)
	{
		file_op = OP_COPY;
	}
	else
	{
		file_op = OP_SYMLINK;

		if(op == CMLO_LINK_REL)
		{
			char dst_dir[PATH_MAX];

			copy_str(dst_dir, sizeof(dst_dir), dst);
			remove_last_path_component(dst_dir);

			copy_str(rel_path, sizeof(rel_path), make_rel_path(src, dst_dir));
			src = rel_path;
		}
	}

	result = perform_operation(file_op, ops, cancellable ? NULL : (void *)1, src,
			dst);
	if(result == 0 && !bg)
	{
		add_operation(file_op, NULL, NULL, src, dst);
	}
	return result;
}

/* Frees background arguments structure with all its data. */
static void
free_bg_args(bg_args_t *args)
{
	free_string_array(args->list, args->nlines);
	free_string_array(args->sel_list, args->sel_list_len);
	free(args->is_in_trash);
	free_ops(args->ops);
	free(args);
}

/* Fills basic fields of the args structure. */
static void
general_prepare_for_bg_task(FileView *view, bg_args_t *args)
{
	dir_entry_t *entry;

	entry = NULL;
	while(iter_marked_entries(view, &entry))
	{
		char full_path[PATH_MAX];

		get_full_path_of(entry, sizeof(full_path), full_path);
		args->sel_list_len = add_to_string_array(&args->sel_list,
				args->sel_list_len, 1, full_path);
	}

	ui_view_reset_selection_and_reload(view);
}

static void
go_to_first_file(FileView *view, char **names, int count)
{
	int i;

	load_saving_pos(view, 1);

	for(i = 0; i < view->list_rows; i++)
	{
		if(is_in_string_array(names, count, view->dir_entry[i].name))
		{
			view->list_pos = i;
			break;
		}
	}
	redraw_view(view);
}

int
make_dirs(FileView *view, int at, char **names, int count, int create_parent)
{
	char buf[COMMAND_GROUP_INFO_LEN + 1];
	int i;
	int n;
	void *cp;
	const char *const dst_dir = get_dst_dir(view, at);

	if(!can_add_files_to_view(view, at))
	{
		return 1;
	}

	cp = (void *)(size_t)create_parent;

	for(i = 0; i < count; ++i)
	{
		char full[PATH_MAX];

		if(is_in_string_array(names, i, names[i]))
		{
			status_bar_errorf("Name \"%s\" duplicates", names[i]);
			return 1;
		}
		if(names[i][0] == '\0')
		{
			status_bar_errorf("Name #%d is empty", i + 1);
			return 1;
		}

		to_canonic_path(names[i], dst_dir, full, sizeof(full));
		if(path_exists(full, NODEREF))
		{
			status_bar_errorf("File \"%s\" already exists", names[i]);
			return 1;
		}
	}

	ui_cancellation_reset();

	snprintf(buf, sizeof(buf), "mkdir in %s: ", replace_home_part(dst_dir));

	get_group_file_list(names, count, buf);
	cmd_group_begin(buf);
	n = 0;
	for(i = 0; i < count && !ui_cancellation_requested(); ++i)
	{
		char full[PATH_MAX];
		to_canonic_path(names[i], dst_dir, full, sizeof(full));

		if(perform_operation(OP_MKDIR, NULL, cp, full, NULL) == 0)
		{
			add_operation(OP_MKDIR, cp, NULL, full, "");
			++n;
		}
		else if(i == 0)
		{
			--i;
			++names;
			--count;
		}
	}
	cmd_group_end();

	if(count > 0)
	{
		if(create_parent)
		{
			for(i = 0; i < count; ++i)
			{
				break_at(names[i], '/');
			}
		}
		go_to_first_file(view, names, count);
	}

	status_bar_messagef("%d director%s created%s", n, (n == 1) ? "y" : "ies",
			get_cancellation_suffix());
	return 1;
}

int
make_files(FileView *view, int at, char *names[], int count)
{
	int i;
	int n;
	char buf[COMMAND_GROUP_INFO_LEN + 1];
	ops_t *ops;
	const char *const dst_dir = get_dst_dir(view, at);

	if(!can_add_files_to_view(view, at))
	{
		return 0;
	}

	for(i = 0; i < count; ++i)
	{
		char full[PATH_MAX];

		if(is_in_string_array(names, i, names[i]))
		{
			status_bar_errorf("Name \"%s\" duplicates", names[i]);
			return 1;
		}
		if(names[i][0] == '\0')
		{
			status_bar_errorf("Name #%d is empty", i + 1);
			return 1;
		}

		to_canonic_path(names[i], dst_dir, full, sizeof(full));
		if(path_exists(full, NODEREF))
		{
			status_bar_errorf("File \"%s\" already exists", names[i]);
			return 1;
		}
	}

	ui_cancellation_reset();

	ops = get_ops(OP_MKFILE, "touching", dst_dir, dst_dir);

	snprintf(buf, sizeof(buf), "touch in %s: ", replace_home_part(dst_dir));

	get_group_file_list(names, count, buf);
	cmd_group_begin(buf);
	n = 0;
	for(i = 0; i < count && !ui_cancellation_requested(); ++i)
	{
		char full[PATH_MAX];
		to_canonic_path(names[i], dst_dir, full, sizeof(full));

		if(perform_operation(OP_MKFILE, ops, NULL, full, NULL) == 0)
		{
			add_operation(OP_MKFILE, NULL, NULL, full, "");
			++n;
		}
	}
	cmd_group_end();

	if(n > 0)
	{
		go_to_first_file(view, names, count);
	}

	status_bar_messagef("%d file%s created%s", n, (n == 1) ? "" : "s",
			get_cancellation_suffix());

	free_ops(ops);

	return 1;
}

/* Fills undo message buffer with names of marked files.  buf should be at least
 * COMMAND_GROUP_INFO_LEN characters length.  fnames can be NULL. */
static void
append_marked_files(FileView *view, char buf[], char **fnames)
{
	const int custom_fnames = (fnames != NULL);
	size_t len = strlen(buf);
	dir_entry_t *entry = NULL;
	while(iter_marked_entries(view, &entry) && len < COMMAND_GROUP_INFO_LEN)
	{
		append_fname(buf, len, entry->name);
		len = strlen(buf);

		if(custom_fnames)
		{
			const char *const custom_fname = *fnames++;

			strncat(buf, " to ", COMMAND_GROUP_INFO_LEN - len - 1);
			len = strlen(buf);
			strncat(buf, custom_fname, COMMAND_GROUP_INFO_LEN - len - 1);
			len = strlen(buf);
		}
	}
}

/* Appends file name to undo message buffer.  buf should be at least
 * COMMAND_GROUP_INFO_LEN characters length. */
static void
append_fname(char buf[], size_t len, const char fname[])
{
	if(buf[len - 2] != ':')
	{
		strncat(buf, ", ", COMMAND_GROUP_INFO_LEN - len - 1);
		len = strlen(buf);
	}
	strncat(buf, fname, COMMAND_GROUP_INFO_LEN - len - 1);
}

int
restore_files(FileView *view)
{
	int m, n;
	dir_entry_t *entry;

	/* This is general check for regular views only. */
	if(!flist_custom_active(view) && !is_trash_directory(view->curr_dir))
	{
		show_error_msg("Restore error", "Not a top-level trash directory.");
		return 0;
	}

	move_cursor_out_of(view, FLS_SELECTION);

	ui_cancellation_reset();

	cmd_group_begin("restore: ");
	cmd_group_end();

	m = 0;
	n = 0;
	entry = NULL;
	while(iter_marked_entries(view, &entry) && !ui_cancellation_requested())
	{
		char full_path[PATH_MAX];
		get_full_path_of(entry, sizeof(full_path), full_path);

		if(is_trash_directory(entry->origin) && restore_from_trash(full_path) == 0)
		{
			++m;
		}
		++n;
	}

	ui_view_schedule_reload(view);

	status_bar_messagef("Restored %d of %d%s", m, n, get_cancellation_suffix());
	return 1;
}

/* Provides different suffixes depending on whether cancellation was requested
 * or not.  Returns pointer to a string literal. */
static const char *
get_cancellation_suffix(void)
{
	return ui_cancellation_requested() ? " (cancelled)" : "";
}

int
can_change_view_files(const FileView *view)
{
	/* TODO: maybe add check whether directory of specific entry is writable for
	 *       custom views. */
	return flist_custom_active(view)
	    || check_if_dir_writable(DR_CURRENT, view->curr_dir);
}

/* Whether set of view files can be extended via addition of new elements.  at
 * parameter is the same as for get_dst_dir().  Returns non-zero if so,
 * otherwise zero is returned. */
static int
can_add_files_to_view(const FileView *view, int at)
{
	if(flist_custom_active(view) && !view->custom.tree_view)
	{
		show_error_msg("Operation error",
				"Custom view can't handle this operation.");
		return 0;
	}

	return check_if_dir_writable(DR_DESTINATION, get_dst_dir(view, at));
}

/* Retrieves root directory of file system sub-tree (for regular or tree views).
 * Returns the path or NULL (for custom views). */
static const char *
get_top_dir(const FileView *view)
{
	if(flist_custom_active(view) && !view->custom.tree_view)
	{
		return NULL;
	}
	return flist_get_dir(view);
}

/* Retrieves current target directory of file system sub-tree.  Root for regular
 * and regular custom views and origin of either active (when at < 0) or
 * specified by its index entry for tree views.  Returns the path. */
static const char *
get_dst_dir(const FileView *view, int at)
{
	if(flist_custom_active(view) && view->custom.tree_view)
	{
		if(at < 0)
		{
			at = view->list_pos;
		}
		else if(at >= view->list_rows)
		{
			at = view->list_rows - 1;
		}
		return view->dir_entry[at].origin;
	}
	return flist_get_dir(view);
}

/* This is a wrapper for is_dir_writable() function, which adds message
 * dialogs.  Returns non-zero if directory can be changed, otherwise zero is
 * returned. */
static int
check_if_dir_writable(DirRole dir_role, const char path[])
{
	if(is_dir_writable(path))
	{
		return 1;
	}

	if(dir_role == DR_DESTINATION)
	{
		show_error_msg("Operation error", "Destination directory is not writable");
	}
	else
	{
		show_error_msg("Operation error", "Current directory is not writable");
	}
	return 0;
}

void
calculate_size_bg(const FileView *view, int force)
{
	int i;

	if(!view->dir_entry[view->list_pos].selected && view->user_selection)
	{
		update_dir_entry_size(view, view->list_pos, force);
		return;
	}

	for(i = 0; i < view->list_rows; ++i)
	{
		const dir_entry_t *const entry = &view->dir_entry[i];

		if(entry->selected && entry->type == FT_DIR)
		{
			update_dir_entry_size(view, i, force);
		}
	}
}

/* Initiates background size calculation for view entry. */
static void
update_dir_entry_size(const FileView *view, int index, int force)
{
	char full_path[PATH_MAX];
	const dir_entry_t *const entry = &view->dir_entry[index];

	if(is_parent_dir(entry->name))
	{
		copy_str(full_path, sizeof(full_path), entry->origin);
	}
	else
	{
		get_full_path_of(entry, sizeof(full_path), full_path);
	}
	start_dir_size_calc(full_path, force);
}

/* Initiates background directory size calculation. */
static void
start_dir_size_calc(const char path[], int force)
{
	char task_desc[PATH_MAX];
	dir_size_args_t *args;

	args = malloc(sizeof(*args));
	args->path = strdup(path);
	args->force = force;

	snprintf(task_desc, sizeof(task_desc), "Calculating size: %s", path);

	if(bg_execute(task_desc, path, BG_UNDEFINED_TOTAL, 0, &dir_size_bg,
				args) != 0)
	{
		free(args->path);
		free(args);

		show_error_msg("Can't calculate size",
				"Failed to initiate background operation");
	}
}

/* Entry point for a background task that calculates size of a directory. */
static void
dir_size_bg(bg_op_t *bg_op, void *arg)
{
	dir_size_args_t *const args = arg;

	dir_size(args->path, args->force);

	free(args->path);
	free(args);
}

/* Calculates directory size and triggers view updates if necessary.  Changes
 * path. */
static void
dir_size(char path[], int force)
{
	(void)calculate_dir_size(path, force);

	remove_last_path_component(path);

	redraw_after_path_change(&lwin, path);
	redraw_after_path_change(&rwin, path);
}

uint64_t
calculate_dir_size(const char path[], int force_update)
{
	DIR* dir;
	struct dirent* dentry;
	const char* slash = "";
	uint64_t size;

	dir = os_opendir(path);
	if(dir == NULL)
	{
		return 0U;
	}

	if(!ends_with_slash(path))
	{
		slash = "/";
	}

	size = 0;
	while((dentry = os_readdir(dir)) != NULL)
	{
		char full_path[PATH_MAX];

		if(is_builtin_dir(dentry->d_name))
		{
			continue;
		}

		snprintf(full_path, sizeof(full_path), "%s%s%s", path, slash,
				dentry->d_name);
		if(is_dir_entry(full_path, dentry))
		{
			uint64_t dir_size;
			dcache_get_at(full_path, &dir_size, NULL);
			if(dir_size == DCACHE_UNKNOWN || force_update)
			{
				dir_size = calculate_dir_size(full_path, force_update);
			}
			size += dir_size;
		}
		else
		{
			size += get_file_size(full_path);
		}
	}

	os_closedir(dir);

	(void)dcache_set_at(path, size, DCACHE_UNKNOWN);
	return size;
}

/* Schedules view redraw in case path change might have affected it. */
static void
redraw_after_path_change(FileView *view, const char path[])
{
	if(path_starts_with(view->curr_dir, path) ||
			flist_custom_active(view))
	{
		ui_view_schedule_redraw(view);
	}
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
