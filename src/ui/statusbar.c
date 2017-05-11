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

#include "statusbar.h"

#include <curses.h> /* mvwin() wbkgdset() werase() */

#include <assert.h> /* assert() */
#include <stdarg.h> /* va_list va_start() va_end() */
#include <stdlib.h> /* free() */
#include <string.h> /* strchr() strcmp() strcpy() strdup() strncpy() */

#include "../cfg/config.h"
#include "../engine/mode.h"
#include "../modes/modes.h"
#include "../modes/more.h"
#include "../utils/macros.h"
#include "../utils/str.h"
#include "../utils/utf8.h"
#include "../status.h"
#include "color_manager.h"
#include "statusline.h"
#include "ui.h"

static void vstatus_bar_messagef(int error, const char format[], va_list ap);
static void status_bar_message_i(const char message[], int error);
static void truncate_with_ellipsis(const char msg[], size_t width,
		char buffer[]);

/* Message displayed on multi-line or too long status bar message. */
static const char PRESS_ENTER_MSG[] = "Press ENTER or type command to continue";

/* Last message that was printed on the statusbar. */
static char *last_message;
static int multiline_status_bar;

void
ui_sb_clear(void)
{
	(void)ui_stat_reposition(1, 0);

	werase(status_bar);
	wresize(status_bar, 1, getmaxx(stdscr) - FIELDS_WIDTH());
	mvwin(status_bar, getmaxy(stdscr) - 1, 0);
	wnoutrefresh(status_bar);

	if(curr_stats.load_stage <= 2)
	{
		multiline_status_bar = 0;
		curr_stats.need_update = UT_FULL;
		return;
	}

	if(multiline_status_bar)
	{
		multiline_status_bar = 0;
		update_screen(UT_FULL);
	}
	multiline_status_bar = 0;
}

void
ui_sb_quick_msgf(const char format[], ...)
{
	va_list ap;

	if(curr_stats.load_stage < 2 || status_bar == NULL)
	{
		return;
	}

	va_start(ap, format);

	checked_wmove(status_bar, 0, 0);
	werase(status_bar);
	vwprintw(status_bar, format, ap);
	wnoutrefresh(status_bar);
	doupdate();

	va_end(ap);
}

void
ui_sb_quick_msg_clear(void)
{
	if(curr_stats.save_msg || is_status_bar_multiline())
	{
		status_bar_message(NULL);
	}
	else
	{
		ui_sb_quick_msgf("%s", "");
	}
}

void
status_bar_message(const char message[])
{
	status_bar_message_i(message, 0);
}

void
status_bar_messagef(const char format[], ...)
{
	va_list ap;

	va_start(ap, format);

	vstatus_bar_messagef(0, format, ap);

	va_end(ap);
}

void
status_bar_error(const char message[])
{
	status_bar_message_i(message, 1);
}

void
status_bar_errorf(const char message[], ...)
{
	va_list ap;

	va_start(ap, message);

	vstatus_bar_messagef(1, message, ap);

	va_end(ap);
}

static void
vstatus_bar_messagef(int error, const char format[], va_list ap)
{
	char buf[1024];

	vsnprintf(buf, sizeof(buf), format, ap);
	status_bar_message_i(buf, error);
}

static void
status_bar_message_i(const char msg[], int error)
{
	/* TODO: Refactor this function status_bar_message_i() */

	static int err;

	int len;
	int lines;
	int status_bar_lines;
	const char *out_msg;
	char truncated_msg[2048];

	if(msg != NULL)
	{
		if(replace_string(&last_message, msg))
		{
			return;
		}

		err = error;

		stats_save_msg(last_message);
	}
	else
	{
		msg = last_message;
	}

	/* We bail out here instead of right at the top to record the message to make
	 * it accessible in tests. */
	if(curr_stats.load_stage == 0)
	{
		return;
	}

	if(msg == NULL || vle_mode_is(CMDLINE_MODE))
	{
		return;
	}

	len = getmaxx(stdscr);
	status_bar_lines = count_lines(msg, len);

	lines = status_bar_lines;
	if(status_bar_lines > 1 || utf8_strsw(msg) > (size_t)getmaxx(status_bar))
	{
		++lines;
	}

	out_msg = msg;

	if(lines > 1)
	{
		if(cfg.trunc_normal_sb_msgs && !err && curr_stats.allow_sb_msg_truncation)
		{
			truncate_with_ellipsis(msg, getmaxx(stdscr) - FIELDS_WIDTH(),
					truncated_msg);
			out_msg = truncated_msg;
			lines = 1;
		}
		else
		{
			const int extra = DIV_ROUND_UP(ARRAY_LEN(PRESS_ENTER_MSG) - 1, len) - 1;
			lines += extra;
		}
	}

	if(lines > getmaxy(stdscr))
	{
		modmore_enter(msg);
		return;
	}

	(void)ui_stat_reposition(lines, 0);
	mvwin(status_bar, getmaxy(stdscr) - lines, 0);
	if(lines == 1)
	{
		wresize(status_bar, lines, getmaxx(stdscr) - FIELDS_WIDTH());
	}
	else
	{
		wresize(status_bar, lines, getmaxx(stdscr));
	}
	checked_wmove(status_bar, 0, 0);

	if(err)
	{
		col_attr_t col = cfg.cs.color[CMD_LINE_COLOR];
		cs_mix_colors(&col, &cfg.cs.color[ERROR_MSG_COLOR]);
		wattron(status_bar, COLOR_PAIR(colmgr_get_pair(col.fg, col.bg)) | col.attr);
	}
	else
	{
		int attr = cfg.cs.color[CMD_LINE_COLOR].attr;
		wattron(status_bar, COLOR_PAIR(cfg.cs.pair[CMD_LINE_COLOR]) | attr);
	}
	werase(status_bar);

	wprint(status_bar, out_msg);
	multiline_status_bar = lines > 1;
	if(multiline_status_bar)
	{
		checked_wmove(status_bar,
				lines - DIV_ROUND_UP(ARRAY_LEN(PRESS_ENTER_MSG), len), 0);
		wclrtoeol(status_bar);
		if(lines < status_bar_lines)
			wprintw(status_bar, "%d of %d lines.  ", lines, status_bar_lines);
		wprintw(status_bar, "%s", PRESS_ENTER_MSG);
	}

	wattrset(status_bar, 0);

	update_all_windows();
	/* This is needed because update_all_windows() doesn't call doupdate() if
	 * curr_stats.load_stage == 1. */
	doupdate();
}

/* Truncate the msg to the width by placing ellipsis in the middle and put the
 * result to the buffer. */
static void
truncate_with_ellipsis(const char msg[], size_t width, char buffer[])
{
	const size_t screen_len = utf8_strsw(msg);
	const size_t screen_left_len = (width - 3)/2;
	const size_t screen_right_len = (width - 3) - screen_left_len;
	const size_t left = utf8_nstrsnlen(msg, screen_left_len);
	const size_t right = utf8_nstrsnlen(msg, screen_len - screen_right_len);
	strncpy(buffer, msg, left);
	strcpy(buffer + left, "...");
	strcpy(buffer + left + 3, msg + right);
	assert(utf8_strsw(buffer) == width);
}

int
is_status_bar_multiline(void)
{
	return multiline_status_bar;
}

const char *
get_last_message(void)
{
	return last_message;
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
