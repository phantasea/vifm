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

#ifndef VIFM__MODES__CMDLINE_H__
#define VIFM__MODES__CMDLINE_H__

#include "../utils/test_helpers.h"

/* Submodes of command-line mode. */
typedef enum
{
	CLS_COMMAND,      /* Regular command-line command. */
	CLS_MENU_COMMAND, /* Menu command-line command. */
	CLS_MENU_FSEARCH, /* Forward search in menu mode. */
	CLS_MENU_BSEARCH, /* Backward search in menu mode. */
	CLS_FSEARCH,      /* Forward search in normal mode. */
	CLS_BSEARCH,      /* Backward search in normal mode. */
	CLS_VFSEARCH,     /* Forward search in visual mode. */
	CLS_VBSEARCH,     /* Backward search in visual mode. */
	CLS_VWFSEARCH,    /* Forward search in view mode. */
	CLS_VWBSEARCH,    /* Backward search in view mode. */
	CLS_FILTER,       /* Filter value. */
	CLS_PROMPT,       /* Input request. */
}
CmdLineSubmode;

/* Callback for prompt input. */
typedef void (*prompt_cb)(const char response[]);

/* Custom prompt line completion function.  arg is user supplied value, which is
 * passed through.  Should return completion offset. */
typedef int (*complete_cmd_func)(const char cmd[], void *arg);

/* Initializes command-line mode. */
void init_cmdline_mode(void);

/* Enters command-line editing mode with specified submode.  cmd specifies
 * initial value, ptr is submode-specific data to be passed back. */
void enter_cmdline_mode(CmdLineSubmode cl_sub_mode, const char cmd[],
		void *ptr);

/* Enters command-line editing mode with prompt submode activated.  cmd
 * specifies initial value, cb is callback called on success, complete is
 * completion function, allow_ee specifies whether issuing external editor is
 * allowed. */
void enter_prompt_mode(const char prompt[], const char cmd[], prompt_cb cb,
		complete_cmd_func complete, int allow_ee);

/* Redraws UI elements of the command-line mode. */
void redraw_cmdline(void);

#ifdef TEST
#include <stddef.h> /* size_t wchar_t */

#include "../cfg/hist.h"
#include "../compat/fs_limits.h"

typedef enum
{
	HIST_NONE,
	HIST_GO,
	HIST_SEARCH
}
HIST;

typedef enum
{
	PS_NORMAL,
	PS_WRONG_PATTERN,
	PS_NO_MATCH,
}
PromptState;

/* Holds state of the command-line editing mode. */
typedef struct
{
	wchar_t *line;                /* the line reading */
	wchar_t *initial_line;        /* initial state of the line */
	int index;                    /* index of the current character */
	int curs_pos;                 /* position of the cursor */
	int len;                      /* length of the string */
	int cmd_pos;                  /* position in the history */
	wchar_t prompt[NAME_MAX + 1]; /* prompt */
	int prompt_wid;               /* width of prompt */
	int complete_continue;        /* if non-zero, continue previous completion */
	int dot_pos;                  /* history pos for dot completion, or < 0 */
	size_t dot_index;             /* dot completion line index */
	size_t dot_len;               /* dot completion previous completion len */
	HIST history_search;          /* HIST_* */
	int hist_search_len;          /* length of history search pattern */
	wchar_t *line_buf;            /* content of line before using history */
	int reverse_completion;
	complete_cmd_func complete;
	int search_mode;
	int old_top;              /* for search_mode */
	int old_pos;              /* for search_mode */
	int line_edited;          /* Cache for whether input line changed flag. */
	int entered_by_mapping;   /* The mode was entered by a mapping. */
	int expanding_abbrev;     /* Abbreviation expansion is in progress. */
	PromptState state;        /* Prompt state with regard to current input. */
}
line_stats_t;
#endif
TSTATIC_DEFS(
	int line_completion(line_stats_t *stat);
	const wchar_t * extract_abbrev(line_stats_t *stat, int *pos, int *no_remap);
	void hist_prev(line_stats_t *stat, const hist_t *hist, size_t len);
	line_stats_t * get_line_stats(void);
)

#endif /* VIFM__MODES__CMDLINE_H__ */

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
