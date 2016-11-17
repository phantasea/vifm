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

#include "grep_menu.h"

#include <stdlib.h> /* free() */
#include <string.h> /* strdup() */

#include "../cfg/config.h"
#include "../modes/dialogs/msg_dialog.h"
#include "../ui/statusbar.h"
#include "../ui/ui.h"
#include "../utils/macros.h"
#include "../utils/path.h"
#include "../utils/str.h"
#include "../macros.h"
#include "menus.h"

static int execute_grep_cb(FileView *view, menu_data_t *m);

int
show_grep_menu(FileView *view, const char args[], int invert)
{
	enum { M_i, M_a, M_s, M_A, M_u, M_U, };

	char *targets;
	int save_msg;
	char *cmd;
	char *escaped_args = NULL;

	custom_macro_t macros[] = {
		[M_i] = { .letter = 'i', .value = NULL, .uses_left = 1, .group = -1 },
		[M_a] = { .letter = 'a', .value = NULL, .uses_left = 1, .group =  1 },
		[M_s] = { .letter = 's', .value = NULL, .uses_left = 1, .group = -1 },
		[M_A] = { .letter = 'A', .value = NULL, .uses_left = 0, .group =  1 },

		[M_u] = { .letter = 'u', .value = "",   .uses_left = 1, .group = -1 },
		[M_U] = { .letter = 'U', .value = "",   .uses_left = 1, .group = -1 },
	};

	static menu_data_t m;

	targets = prepare_targets(view);
	if(targets == NULL)
	{
		show_error_msg("Grep", "Failed to setup target directory.");
		return 0;
	}

	init_menu_data(&m, view, format_str("Grep %s", args),
			format_str("No matches found: %s", args));

	m.stashable = 1;
	m.execute_handler = &execute_grep_cb;
	m.key_handler = &filelist_khandler;

	macros[M_i].value = invert ? "-v" : "";
	macros[M_a].value = args;
	macros[M_s].value = targets;
	macros[M_A].value = args;
	if(args[0] != '-')
	{
		escaped_args = shell_like_escape(args, 0);
		macros[M_a].value = escaped_args;
	}

	cmd = expand_custom_macros(cfg.grep_prg, ARRAY_LEN(macros), macros);

	free(escaped_args);
	free(targets);

	status_bar_message("grep...");
	save_msg = capture_output(view, cmd, 0, &m, macros[M_u].explicit_use,
			macros[M_U].explicit_use);
	free(cmd);

	return save_msg;
}

/* Callback that is called when menu item is selected.  Should return non-zero
 * to stay in menu mode. */
static int
execute_grep_cb(FileView *view, menu_data_t *m)
{
	(void)goto_selected_file(m, view, m->items[m->pos], 1);
	return 1;
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
