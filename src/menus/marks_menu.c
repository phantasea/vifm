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

#include "marks_menu.h"

#include <curses.h> /* getmaxx() */

#include <stddef.h> /* size_t wchar_t */
#include <stdio.h> /* snprintf() */
#include <string.h> /* strdup() strcpy() strlen() */
#include <wchar.h> /* wcscmp() */

#include "../compat/fs_limits.h"
#include "../ui/ui.h"
#include "../utils/fs.h"
#include "../utils/macros.h"
#include "../utils/path.h"
#include "../utils/string_array.h"
#include "../utils/utf8.h"
#include "../marks.h"
#include "menus.h"

static int execute_mark_cb(FileView *view, menu_data_t *m);
static KHandlerResponse mark_khandler(FileView *view, menu_data_t *m,
		const wchar_t keys[]);

int
show_marks_menu(FileView *view, const char marks[])
{
	int active_marks[NUM_MARKS];
	int i;
	size_t max_len;

	static menu_data_t m;
	init_menu_data(&m, view, strdup("Mark -- Directory -- File"),
			strdup("No marks set"));
	m.execute_handler = &execute_mark_cb;
	m.key_handler = &mark_khandler;

	m.len = init_active_marks(marks, active_marks);

	max_len = 0;
	for(i = 0; i < m.len; ++i)
	{
		const mark_t *const mark = get_mark(active_marks[i]);
		const size_t len = utf8_strsw(mark->directory);
		if(len > max_len)
		{
			max_len = len;
		}
	}
	max_len = MIN(max_len + 3, (size_t)(getmaxx(menu_win) - 5 - 2 - 10));

	i = 0;
	while(i < m.len)
	{
		char item_buf[PATH_MAX];
		char *with_tilde;
		int overhead;
		const mark_t *mark;
		const char *file;
		const char *suffix = "";
		const int mn = active_marks[i];

		mark = get_mark(mn);

		with_tilde = replace_home_part(mark->directory);
		if(utf8_strsw(with_tilde) > max_len - 3)
		{
			size_t width = utf8_nstrsnlen(with_tilde, max_len - 6);
			strcpy(with_tilde + width, "...");
		}

		if(!is_valid_mark(mn))
		{
			file = "[invalid]";
		}
		else if(is_parent_dir(mark->file))
		{
			file = "[none]";
		}
		else
		{
			char path[PATH_MAX];

			file = mark->file;

			snprintf(path, sizeof(path), "%s/%s", mark->directory, mark->file);
			if(is_dir(path))
			{
				suffix = "/";
			}
		}

		overhead = utf8_strso(with_tilde);
		snprintf(item_buf, sizeof(item_buf), "%c   %-*s%s%s", index2mark(mn),
				(int)(max_len + overhead), with_tilde, file, suffix);

		i = add_to_string_array(&m.items, i, 1, item_buf);
	}
	m.len = i;

	return display_menu(m.state, view);
}

/* Callback that is called when menu item is selected.  Should return non-zero
 * to stay in menu mode. */
static int
execute_mark_cb(FileView *view, menu_data_t *m)
{
	goto_mark(view, m->items[m->pos][0]);
	return 0;
}

/* Menu-specific shortcut handler.  Returns code that specifies both taken
 * actions and what should be done next. */
static KHandlerResponse
mark_khandler(FileView *view, menu_data_t *m, const wchar_t keys[])
{
	if(wcscmp(keys, L"dd") == 0)
	{
		clear_mark(m->items[m->pos][0]);
		remove_current_item(m->state);
		return KHR_REFRESH_WINDOW;
	}
	return KHR_UNHANDLED;
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
