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

#ifndef VIFM__MACROS_H__
#define VIFM__MACROS_H__

#include <stddef.h> /* size_t */

#include "ui/ui.h"
#include "utils/test_helpers.h"

/* Macros that affect running of commands and processing their output. */
typedef enum
{
	MF_NONE, /* No special macro specified. */

	MF_MENU_OUTPUT,      /* Redirect output to the menu. */
	MF_MENU_NAV_OUTPUT,  /* Redirect output to the navigation menu. */
	MF_STATUSBAR_OUTPUT, /* Redirect output to the status bar. */
	MF_PREVIEW_OUTPUT,   /* Redirect output to the preview. */

	/* Redirect output into custom view. */
	MF_CUSTOMVIEW_OUTPUT,     /* Applying current sorting. */
	MF_VERYCUSTOMVIEW_OUTPUT, /* Not changing ordering. */

	/* Redirect output from interactive application into custom view. */
	MF_CUSTOMVIEW_IOUTPUT,     /* Applying current sorting. */
	MF_VERYCUSTOMVIEW_IOUTPUT, /* Not changing ordering. */

	MF_SPLIT,       /* Run command in a new screen region. */
	MF_IGNORE,      /* Completely ignore command output. */
	MF_NO_TERM_MUX, /* Forbid using terminal multiplexer, even if active. */
}
MacroFlags;

/* Description of a macro for the expand_custom_macros() function. */
typedef struct
{
	char letter;       /* Macro identifier in the pattern. */
	const char *value; /* A value to replace macro with. */
	int uses_left;     /* Number of mandatory uses of the macro for group head. */
	int group;         /* Index of macro group head or -1. */
	int explicit_use;  /* Set to non-zero on explicit expansion. */
}
custom_macro_t;

/* args and flags parameters can equal NULL.  The string returned needs to be
 * freed in the calling function.  After executing flags is one of MF_*
 * values. */
char * expand_macros(const char command[], const char args[], MacroFlags *flags,
		int for_shell);

/* Like expand_macros(), but expands only single element macros and aims for
 * single string, so escaping is disabled. */
char * ma_expand_single(const char command[]);

/* Gets clear part of the viewer.  Returns NULL if there is none, otherwise
 * pointer inside the cmd string is returned. */
const char * ma_get_clear_cmd(const char cmd[]);

/* Expands macros of form %x in the pattern (%% is expanded to %) according to
 * macros specification. */
char * expand_custom_macros(const char pattern[], size_t nmacros,
		custom_macro_t macros[]);

/* Maps flag to corresponding string representation of the macro using %-syntax.
 * Returns the string representation. */
const char * macros_to_str(MacroFlags flags);

#ifdef TEST
#include "engine/cmds.h"
#endif

TSTATIC_DEFS(
	char * append_selected_files(FileView *view, char expanded[],
		int under_cursor, int quotes, const char mod[], int for_shell);
)

#endif /* VIFM__MACROS_H__ */

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
