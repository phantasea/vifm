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

#include "flist_sel.h"

#include <stddef.h> /* NULL */
#include <stdlib.h> /* calloc() free() */
#include <string.h> /* strdup() */

#include "cfg/config.h"
#include "modes/dialogs/msg_dialog.h"
#include "ui/fileview.h"
#include "ui/statusbar.h"
#include "ui/ui.h"
#include "utils/fs.h"
#include "utils/matchers.h"
#include "utils/path.h"
#include "utils/str.h"
#include "utils/string_array.h"
#include "utils/trie.h"
#include "filelist.h"
#include "flist_pos.h"
#include "running.h"

static void save_selection(FileView *view);
static void free_saved_selection(FileView *view);
static void select_unselect_entry(FileView *view, dir_entry_t *entry,
		int select);

void
flist_sel_stash(FileView *view)
{
	save_selection(view);
	flist_sel_drop(view);
}

void
flist_sel_drop(FileView *view)
{
	int i;
	for(i = 0; i < view->list_rows; ++i)
	{
		view->dir_entry[i].selected = 0;
	}
	view->selected_files = 0;
}

void
flist_sel_view_reloaded(FileView *view, int location_changed)
{
	if(location_changed)
	{
		free_saved_selection(view);
	}
	else
	{
		save_selection(view);
	}
	flist_sel_drop(view);
}

/* Collects currently selected files in view->saved_selection array.  Use
 * free_saved_selection() to clean up memory allocated by this function. */
static void
save_selection(FileView *view)
{
	int i;
	dir_entry_t *entry;

	free_saved_selection(view);

	flist_sel_recount(view);
	if(view->selected_files == 0)
	{
		return;
	}

	view->saved_selection = calloc(view->selected_files, sizeof(char *));
	if(view->saved_selection == NULL)
	{
		show_error_msg("Memory Error", "Unable to allocate enough memory");
		return;
	}

	i = 0;
	entry = NULL;
	while(iter_selected_entries(view, &entry))
	{
		char full_path[PATH_MAX];

		get_full_path_of(entry, sizeof(full_path), full_path);
		view->saved_selection[i] = strdup(full_path);
		if(view->saved_selection[i] == NULL)
		{
			show_error_msg("Memory Error", "Unable to allocate enough memory");
			break;
		}

		++i;
	}
	view->nsaved_selection = i;
}

/* Frees list of previously selected files. */
static void
free_saved_selection(FileView *view)
{
	free_string_array(view->saved_selection, view->nsaved_selection);
	view->nsaved_selection = 0;
	view->saved_selection = NULL;
}

void
flist_sel_invert(FileView *view)
{
	int i;
	view->selected_files = 0;
	for(i = 0; i < view->list_rows; ++i)
	{
		dir_entry_t *const e = &view->dir_entry[i];
		if(fentry_is_valid(e))
		{
			e->selected = !e->selected;
		}
		view->selected_files += (e->selected != 0);
	}
}

void
flist_sel_stash_if_nonempty(FileView *view)
{
	if(view->selected_files != 0)
	{
		flist_sel_stash(view);
		ui_view_schedule_redraw(view);
	}
}

void
flist_sel_restore(FileView *view, reg_t *reg)
{
	int i;
	trie_t *const selection_trie = trie_create();

	flist_sel_drop(view);

	if(reg == NULL)
	{
		int i;
		for(i = 0; i < view->nsaved_selection; ++i)
		{
			(void)trie_put(selection_trie, view->saved_selection[i]);
		}
	}
	else
	{
		int i;
		for(i = 0; i < reg->nfiles; ++i)
		{
			(void)trie_put(selection_trie, reg->files[i]);
		}
	}

	for(i = 0; i < view->list_rows; ++i)
	{
		char full_path[PATH_MAX];
		void *ignored_data;
		dir_entry_t *const entry = &view->dir_entry[i];

		get_full_path_of(entry, sizeof(full_path), full_path);
		if(trie_get(selection_trie, full_path, &ignored_data) == 0)
		{
			entry->selected = 1;
			++view->selected_files;

			/* Assuming that selection is usually contiguous it makes sense to quit
			 * when we found all elements to optimize this operation. */
			if(view->selected_files == view->nsaved_selection)
			{
				break;
			}
		}
	}

	trie_free(selection_trie);

	redraw_current_view();
}

void
flist_sel_recount(FileView *view)
{
	int i;

	view->selected_files = 0;
	for(i = 0; i < view->list_rows; ++i)
	{
		view->selected_files += (view->dir_entry[i].selected != 0);
	}
}

void
flist_sel_by_range(FileView *view, int begin, int end, int select)
{
	select = (select != 0);

	if(begin < 0)
	{
		select_unselect_entry(view, get_current_entry(view), select);
	}
	else
	{
		int i;
		for(i = begin; i <= end; ++i)
		{
			select_unselect_entry(view, &view->dir_entry[i], select);
		}
	}

	ui_view_schedule_redraw(view);
}

/* Selects or unselects the entry. */
static void
select_unselect_entry(FileView *view, dir_entry_t *entry, int select)
{
	if(fentry_is_valid(entry) && (entry->selected != 0) != select)
	{
		entry->selected = select;
		view->selected_files += (select ? 1 : -1);
	}
}

int
flist_sel_by_filter(FileView *view, const char cmd[], int erase_old, int select)
{
	trie_t *selection_trie;
	char **files;
	int nfiles;
	int i;

	char *const expanded_cmd = expand_macros(cmd, NULL, NULL, 1);

	if(run_cmd_for_output(expanded_cmd, &files, &nfiles) != 0)
	{
		free(expanded_cmd);
		status_bar_error("Failed to start/read output of external command");
		return 1;
	}
	free(expanded_cmd);

	/* Append to previous selection unless ! is specified. */
	if(select && erase_old)
	{
		flist_sel_drop(view);
	}

	if(nfiles == 0)
	{
		free_string_array(files, nfiles);
		return 0;
	}

	/* Compose trie out of absolute paths of files to [un]select. */
	selection_trie = trie_create();
	for(i = 0; i < nfiles; ++i)
	{
		char canonic_path[PATH_MAX];
		to_canonic_path(files[i], flist_get_dir(view), canonic_path,
				sizeof(canonic_path));
		(void)trie_put(selection_trie, canonic_path);
	}
	free_string_array(files, nfiles);

	/* [un]select files that match the list. */
	select = (select != 0);
	for(i = 0; i < view->list_rows; ++i)
	{
		char full_path[PATH_MAX];
		void *ignored_data;
		dir_entry_t *const entry = &view->dir_entry[i];

		if((entry->selected != 0) == select)
		{
			continue;
		}

		get_full_path_of(entry, sizeof(full_path), full_path);
		if(trie_get(selection_trie, full_path, &ignored_data) == 0)
		{
			entry->selected = select;
			view->selected_files += (select ? 1 : -1);
		}
	}

	trie_free(selection_trie);

	ui_view_schedule_redraw(view);
	return 0;
}

int
flist_sel_by_pattern(FileView *view, const char pattern[], int erase_old,
		int select)
{
	int i;
	char *error;
	matchers_t *const ms = matchers_alloc(pattern, 0, 1,
			cfg_get_last_search_pattern(), &error);
	if(ms == NULL)
	{
		status_bar_errorf("Pattern error: %s", error);
		free(error);
		return 1;
	}

	select = (select != 0);

	/* Append to previous selection unless ! is specified. */
	if(select && erase_old)
	{
		flist_sel_drop(view);
	}

	for(i = 0; i < view->list_rows; ++i)
	{
		char file_path[PATH_MAX];
		dir_entry_t *const entry = &view->dir_entry[i];

		if((entry->selected != 0) == select)
		{
			continue;
		}

		get_full_path_of(entry, sizeof(file_path) - 1U, file_path);

		if(matchers_match(ms, file_path) ||
				(fentry_is_dir(entry) && matchers_match_dir(ms, file_path)))
		{
			entry->selected = select;
			view->selected_files += (select ? 1 : -1);
		}
	}

	matchers_free(ms);

	ui_view_schedule_redraw(view);
	return 0;
}

void
flist_sel_count(FileView *view, int at, int count)
{
	/* Use current position if none given. */
	if(at < 0)
	{
		at = view->list_pos;
	}

	flist_sel_stash(view);

	while(count-- > 0 && at < view->list_rows)
	{
		if(fentry_is_valid(&view->dir_entry[at]))
		{
			view->dir_entry[at].selected = 1;
			++view->selected_files;
		}
		++at;
	}
}

int
flist_sel_range(FileView *view, int begin, int end, int select_current)
{
	/* Both starting and ending range positions are given. */
	if(begin > -1)
	{
		int i;
		flist_sel_stash(view);

		for(i = begin; i <= end; ++i)
		{
			if(fentry_is_valid(&view->dir_entry[i]))
			{
				view->dir_entry[i].selected = 1;
				++view->selected_files;
			}
		}
		return view->selected_files > 0;
	}

	if(view->selected_files != 0)
	{
		return 0;
	}

	/* XXX: is it possible that begin <= -1 and end > -1? */
	if(end > -1)
	{
		flist_sel_stash(view);

		if(fentry_is_valid(&view->dir_entry[end]))
		{
			view->dir_entry[end].selected = 1;
			view->selected_files = 1;
		}
	}
	else if(select_current)
	{
		flist_sel_stash(view);

		/* The front check is for tests. */
		if(view->list_pos < view->list_rows &&
				fentry_is_valid(&view->dir_entry[view->list_pos]))
		{
			view->dir_entry[view->list_pos].selected = 1;
			view->selected_files = 1;
		}
	}

	return view->selected_files > 0;
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 : */
