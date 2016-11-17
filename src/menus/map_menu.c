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

#include "map_menu.h"

#include <curses.h> /* KEY_* */

#include <stddef.h> /* wchar_t */
#include <stdlib.h> /* malloc() free() */
#include <string.h> /* strdup() strlen() strcat() */
#include <wchar.h> /* wcsncmp() wcslen() */

#include "../compat/reallocarray.h"
#include "../engine/keys.h"
#include "../modes/modes.h"
#include "../ui/ui.h"
#include "../utils/str.h"
#include "../utils/string_array.h"
#include "../bracket_notation.h"
#include "menus.h"

static void add_mapping_item(const wchar_t lhs[], const wchar_t rhs[],
		const char descr[]);

/* Menu object is global to make it available in add_mapping_item(). */
static menu_data_t m;

/* Prefix to check LHS against. */
static const wchar_t *prefix;
/* Length of the prefix string. */
static size_t prefix_len;

int
show_map_menu(FileView *view, const char mode_str[], int mode,
		const wchar_t start[])
{
	const int dialogs = mode == SORT_MODE || mode == ATTR_MODE
	                 || mode == CHANGE_MODE || mode == FILE_INFO_MODE;

	init_menu_data(&m, view,
			format_str("Mappings for %s mode%s", mode_str, dialogs ? "s" : ""),
			strdup("No mappings found"));

	prefix = start;
	prefix_len = wcslen(prefix);

	vle_keys_list(mode, &add_mapping_item, dialogs);

	return display_menu(m.state, view);
}

/* Adds matching key information to the menu after pre-formatting. */
static void
add_mapping_item(const wchar_t lhs[], const wchar_t rhs[], const char descr[])
{
	enum { MAP_WIDTH = 11 };

	char *mb_lhs;

	if(wcsncmp(prefix, lhs, prefix_len) != 0)
	{
		return;
	}

	/* Handle empty RHS, but don't affect separator line. */
	if(rhs[0] == L'\0' && lhs[0] != L'\0' && descr[0] == '\0')
	{
		rhs = L"<nop>";
	}

	mb_lhs = wstr_to_spec(lhs);

	m.items = reallocarray(m.items, m.len + 1, sizeof(char *));

	if(rhs[0] == L'\0')
	{
		m.items[m.len++] = format_str("%-*s %s", MAP_WIDTH, mb_lhs, descr);
	}
	else
	{
		char *const mb_rhs = wstr_to_spec(rhs);
		m.items[m.len++] = format_str("%-*s %s", MAP_WIDTH, mb_lhs, mb_rhs);
		free(mb_rhs);
	}

	free(mb_lhs);
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
