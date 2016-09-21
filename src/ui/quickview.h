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

#ifndef VIFM__UI__QUICKVIEW_H__
#define VIFM__UI__QUICKVIEW_H__

#include <stddef.h> /* size_t */
#include <stdio.h> /* FILE */

#include "../utils/test_helpers.h"
#include "ui.h"

void quick_view_file(FileView *view);

void toggle_quick_view(void);

/* Quits preview pane or view modes. */
void preview_close(void);

FILE * use_info_prog(const char viewer[]);

/* Performs view clearing with the given command. */
void qv_cleanup(FileView *view, const char cmd[]);

/* Gets viewer command for a file considering its type (directory vs. file).
 * Returns NULL if no suitable viewer available, otherwise returns pointer to
 * string stored internally. */
const char * qv_get_viewer(const char path[]);

/* Previews directory, actual preview is to be read from returned stream.
 * Returns the stream or NULL on error. */
FILE * qv_view_dir(const char path[]);

/* Decides on path that should be explored when cursor points to the given
 * entry. */
void qv_get_path_to_explore(const dir_entry_t *entry, char buf[],
		size_t buf_len);

TSTATIC_DEFS(
	void view_stream(FILE *fp, int wrapped);
);

#endif /* VIFM__UI__QUICKVIEW_H__ */

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
