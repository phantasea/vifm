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

#include "ratings_menu.h"

#include <curses.h> /* getmaxx() */

#include <stdio.h> /* snprintf() */
#include <string.h> /* strdup() strcpy() strlen() */

#include "../ui/ui.h"
#include "../utils/path.h"
#include "../utils/str.h"
#include "../utils/string_array.h"
#include "menus.h"

static int execute_ratings_cb(view_t *view, menu_data_t *m);
extern rating_entry_t * get_rating_list();

int show_ratings_menu(view_t *view)
{
	static menu_data_t m;
	menus_init_data(&m, view, strdup("Rating Stars -- Target"), strdup("No star ratings added"));
	m.execute_handler = &execute_ratings_cb;

	rating_entry_t *entry = get_rating_list();
	while (entry != NULL)
	{
		if (entry->star <= 0 || entry->path == NULL)
		{
			entry = entry->next;
			continue;
		}

		char item_buf[PATH_MAX + 1];
		snprintf(item_buf, sizeof(item_buf), "%d %s", entry->star, entry->path);

		m.len = add_to_string_array(&m.items, m.len, item_buf);

		entry = entry->next;
	}

	return menus_enter(m.state, view);
}

/* Callback that is called when menu item is selected. 
	 Should return non-zero to stay in menu mode. */
static int
execute_ratings_cb(view_t *view, menu_data_t *m)
{
	(void)menus_goto_file(m, view, m->items[m->pos]+2, 0);
	return 0;
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
