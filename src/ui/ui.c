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

#include "ui.h"

#include <curses.h> /* mvwin() wbkgdset() werase() */

#ifndef _WIN32
#include <sys/ioctl.h>
#include <termios.h> /* struct winsize */
#endif
#include <sys/time.h> /* gettimeofday() */
#include <unistd.h>

#include <assert.h> /* assert() */
#include <ctype.h> /* isdigit() */
#include <errno.h> /* errno */
#include <stddef.h> /* NULL size_t wchar_t */
#include <stdint.h> /* uint64_t */
#include <stdlib.h> /* abs() free() */
#include <stdio.h> /* snprintf() vsnprintf() */
#include <string.h> /* memset() strcat() strcmp() strcpy() strdup() strlen() */
#include <time.h> /* time() */
#include <wchar.h> /* wint_t wcslen() */

#include "../cfg/config.h"
#include "../cfg/info.h"
#include "../compat/curses.h"
#include "../compat/fs_limits.h"
#include "../compat/pthread.h"
#include "../engine/mode.h"
#include "../int/term_title.h"
#include "../modes/dialogs/msg_dialog.h"
#include "../modes/modes.h"
#include "../modes/view.h"
#include "../modes/wk.h"
#include "../utils/fs.h"
#include "../utils/log.h"
#include "../utils/macros.h"
#include "../utils/matchers.h"
#include "../utils/path.h"
#include "../utils/str.h"
#include "../utils/utf8.h"
#include "../utils/utils.h"
#include "../event_loop.h"
#include "../filelist.h"
#include "../flist_sel.h"
#include "../opt_handlers.h"
#include "../status.h"
#include "../vifm.h"
#include "private/statusline.h"
#include "cancellation.h"
#include "color_manager.h"
#include "color_scheme.h"
#include "colors.h"
#include "fileview.h"
#include "quickview.h"
#include "statusbar.h"
#include "statusline.h"

/* Type of path transformation function for format_view_title(). */
typedef char * (*path_func)(const char[]);

/* Mutexes for views, located out of FileView so that they are never moved nor
 * copied, which would yield undefined behaviour. */
static pthread_mutex_t lwin_timestamps_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t rwin_timestamps_mutex = PTHREAD_MUTEX_INITIALIZER;

FileView lwin = { .timestamps_mutex = &lwin_timestamps_mutex };
FileView rwin = { .timestamps_mutex = &rwin_timestamps_mutex };

static void create_windows(void);
static void update_geometry(void);
static int get_working_area_height(void);
static void clear_border(WINDOW *border);
static int middle_border_is_visible(void);
static void update_views(int reload);
static void reload_lists(void);
static void reload_list(FileView *view);
static void update_view(FileView *view);
static void update_window_lazy(WINDOW *win);
static void update_term_size(void);
static void update_statusbar_layout(void);
static int get_ruler_width(FileView *view);
static char * expand_ruler_macros(FileView *view, const char format[]);
static void switch_panes_content(void);
static void update_origins(FileView *view, const char *old_main_origin);
static void set_splitter(int pos);
static char * path_identity(const char path[]);
static char * format_view_title(const FileView *view, path_func pf);
static void print_view_title(const FileView *view, int active_view,
		char title[]);
static void fixup_titles_attributes(const FileView *view, int active_view);
static uint64_t get_updated_time(uint64_t prev);

void
ui_ruler_update(FileView *view, int lazy_redraw)
{
	char *expanded;

	update_statusbar_layout();

	expanded = expand_ruler_macros(view, cfg.ruler_format);
	expanded = break_in_two(expanded, getmaxx(ruler_win));

	ui_ruler_set(expanded);
	if(!lazy_redraw)
	{
		wrefresh(ruler_win);
	}

	free(expanded);
}

void
ui_ruler_set(const char val[])
{
	const int x = getmaxx(ruler_win) - strlen(val);

	werase(ruler_win);
	mvwaddstr(ruler_win, 0, MAX(x, 0), val);
	wnoutrefresh(ruler_win);
}

int
setup_ncurses_interface(void)
{
	int screen_x, screen_y;

	initscr();
	noecho();
	nonl();
	raw();

	curs_set(0);

	getmaxyx(stdscr, screen_y, screen_x);
	/* screen is too small to be useful*/
	if(screen_y < MIN_TERM_HEIGHT || screen_x < MIN_TERM_WIDTH)
	{
		vifm_finish("Terminal is too small to run vifm.");
	}

	if(!has_colors())
		vifm_finish("Vifm requires a console that can support color.");

	start_color();
	use_default_colors();

	cs_load_defaults();

	create_windows();

	cfg.tab_stop = TABSIZE;

#ifdef ENABLE_EXTENDED_KEYS
	keypad(status_bar, TRUE);
#endif /* ENABLE_EXTENDED_KEYS */

#if defined(NCURSES_EXT_FUNCS) && NCURSES_EXT_FUNCS >= 20081102
#ifdef HAVE_SET_ESCDELAY_FUNC
	/* Use ncurses specific function to disable delay after pressing escape key */
	set_escdelay(0);
#endif
#endif

	update_geometry();

	return 1;
}

/* Initializes all WINDOW variables by calling newwin() to create ncurses
 * windows. */
static void
create_windows(void)
{
	menu_win = newwin(1, 1, 0, 0);
	sort_win = newwin(1, 1, 0, 0);
	change_win = newwin(1, 1, 0, 0);
	error_win = newwin(1, 1, 0, 0);

	lwin.title = newwin(1, 1, 0, 0);
	lwin.win = newwin(1, 1, 0, 0);

	mborder = newwin(1, 1, 0, 0);

	top_line = newwin(1, 1, 0, 0);

	rwin.title = newwin(1, 1, 0, 0);
	rwin.win = newwin(1, 1, 0, 0);

	stat_win = newwin(1, 1, 0, 0);
	job_bar = newwin(1, 1, 0, 0);
	status_bar = newwin(1, 1, 0, 0);
	ruler_win = newwin(1, 1, 0, 0);
	input_win = newwin(1, 1, 0, 0);
}

void
ui_update_term_state(void)
{
	int screen_x, screen_y;

	update_term_size();
	getmaxyx(stdscr, screen_y, screen_x);
	(void)stats_update_term_state(screen_x, screen_y);
}

int
ui_char_pressed(wint_t c)
{
	wint_t pressed = L'\0';
	const int cancellation_state = ui_cancellation_pause();

	/* Query single character in non-blocking mode. */
	wtimeout(status_bar, 0);
	if(compat_wget_wch(status_bar, &pressed) != ERR)
	{
		if(pressed != c && pressed != NC_C_c)
		{
			compat_unget_wch(pressed);
		}
	}

	ui_cancellation_resume(cancellation_state);

	if(c != NC_C_c && pressed == NC_C_c)
	{
		ui_cancellation_request();
	}

	return pressed == c;
}

static void
correct_size(FileView *view)
{
	int x, y;

	getmaxyx(view->win, y, x);
	view->window_width = x - 1;
	view->window_rows = y - 1;
	view->column_count = calculate_columns_count(view);
	view->window_cells = view->column_count*y;
}

/* Updates TUI elements sizes and coordinates for single window
 * configuration. */
static void
only_layout(FileView *view, int screen_x)
{
	//mod by sim1
	wresize(view->title, 1, screen_x);
	mvwin(view->title, 0, 0);

	wresize(view->win, get_working_area_height(), screen_x);
	mvwin(view->win, 1, 0);
}

/* Updates TUI elements sizes and coordinates for vertical configuration of
 * panes: left one and right one. */
static void
vertical_layout(int screen_x)
{
	const int border_height = get_working_area_height();

	int splitter_pos;
	int splitter_width;

	if(curr_stats.splitter_pos < 0)
		splitter_pos = screen_x/2 - 1 + screen_x%2;
	else
		splitter_pos = curr_stats.splitter_pos;

	//mod by sim1
	splitter_width = 1 - screen_x%2;
	if(splitter_pos < 4)
		splitter_pos = 4;
	if(splitter_pos > screen_x - 4 - splitter_width)
		splitter_pos = screen_x - 4 - splitter_width;
	if(curr_stats.splitter_pos >= 0)
		curr_stats.splitter_pos = splitter_pos;

	//mod by sim1
	wresize(lwin.title, 1, splitter_pos);
	mvwin(lwin.title, 0, 0);

	wresize(lwin.win, border_height, splitter_pos);
	mvwin(lwin.win, 1, 0);

	wbkgdset(mborder, COLOR_PAIR(cfg.cs.pair[BORDER_COLOR]) | cfg.cs.color[BORDER_COLOR].attr);
	wresize(mborder, border_height, splitter_width);
	mvwin(mborder, 1, splitter_pos);

	wresize(top_line, 1, splitter_width);
	mvwin(top_line, 0, splitter_pos);

	//mod by sim1
	wresize(rwin.title, 1, screen_x - (splitter_pos + splitter_width));
	mvwin(rwin.title, 0, splitter_pos + splitter_width);

	wresize(rwin.win, border_height, screen_x - (splitter_pos + splitter_width));
	mvwin(rwin.win, 1, splitter_pos + splitter_width);
}

/* Updates TUI elements sizes and coordinates for horizontal configuration of
 * panes: top one and bottom one. */
static void
horizontal_layout(int screen_x, int screen_y)
{
	int splitter_pos;

	if(curr_stats.splitter_pos < 0)
		splitter_pos = screen_y/2 - 1;
	else
		splitter_pos = curr_stats.splitter_pos;
	if(splitter_pos < 2)
		splitter_pos = 2;
	if(splitter_pos > get_working_area_height() - 1)
		splitter_pos = get_working_area_height() - 1;
	if(curr_stats.splitter_pos >= 0)
		curr_stats.splitter_pos = splitter_pos;

	//mod by sim1
	wresize(lwin.title, 1, screen_x);
	mvwin(lwin.title, 0, 0);

	//mod by sim1
	wresize(rwin.title, 1, screen_x);
	mvwin(rwin.title, splitter_pos, 0);

	wresize(lwin.win, splitter_pos - 1, screen_x);
	mvwin(lwin.win, 1, 0);

	wresize(rwin.win, get_working_area_height() - splitter_pos, screen_x);
	mvwin(rwin.win, splitter_pos + 1, 0);

	wbkgdset(mborder, COLOR_PAIR(cfg.cs.pair[BORDER_COLOR]) | cfg.cs.color[BORDER_COLOR].attr);
	wresize(mborder, 1, screen_x);
	mvwin(mborder, splitter_pos, 0);

	wresize(top_line, 1, 2 - screen_x%2);
	mvwin(top_line, 0, screen_x/2 - 1 + screen_x%2);
}

static void
resize_all(void)
{
	static float prev_x = -1.f, prev_y = -1.f;

	int screen_x, screen_y;

	update_geometry();
	getmaxyx(stdscr, screen_y, screen_x);

	LOG_INFO_MSG("screen_y = %d; screen_x = %d", screen_y, screen_x);

	if(stats_update_term_state(screen_x, screen_y) != TS_NORMAL)
	{
		return;
	}

	if(prev_x < 0)
	{
		prev_x = screen_x;
		prev_y = screen_y;
	}

	if(curr_stats.splitter_pos >= 0)
	{
		if(curr_stats.split == HSPLIT)
			curr_stats.splitter_pos *= screen_y/prev_y;
		else
			curr_stats.splitter_pos *= screen_x/prev_x;
	}

	prev_x = screen_x;
	prev_y = screen_y;

	wresize(stdscr, screen_y, screen_x);
	wresize(menu_win, screen_y - 1, screen_x);

	/* These need a resize at least after terminal size was zero or they grow and
	 * produce bad looking effect. */
	if(curr_stats.number_of_windows == 1)
	{
		//mod by sim1
		only_layout(&lwin, screen_x);
		only_layout(&rwin, screen_x);
	}
	else
	{
		if(curr_stats.split == HSPLIT)
			horizontal_layout(screen_x, screen_y);
		else
			vertical_layout(screen_x);
	}

	correct_size(&lwin);
	correct_size(&rwin);

	wresize(stat_win, 1, screen_x);
	(void)ui_stat_reposition(1, 0);

	wresize(job_bar, 1, screen_x);

	update_statusbar_layout();

	curs_set(0);
}

/* Calculates height available for main area that contains file lists.  Returns
 * the height. */
static int
get_working_area_height(void)
{
	return getmaxy(stdscr)                  /* Total available height. */
	     - 1                                /* Top line. */
	     - (cfg.display_statusline ? 1 : 0) /* Status line. */
	     - ui_stat_job_bar_height()         /* Job bar. */
	     - 1;                               /* Status bar line. */
}

/* Updates internal data structures to reflect actual terminal geometry. */
static void
update_geometry(void)
{
	int screen_x, screen_y;

	update_term_size();

#ifdef _WIN32
	/* This resizes vifm to window size. */
	resize_term(0, 0);
#endif

	getmaxyx(stdscr, screen_y, screen_x);
	cfg.lines = screen_y;
	cfg.columns = screen_x;

	LOG_INFO_MSG("New geometry: %dx%d", screen_x, screen_y);

	if(curr_stats.initial_lines == INT_MIN)
	{
		curr_stats.initial_lines = screen_y;
		curr_stats.initial_columns = screen_x;
	}

	load_geometry();
}

int
cv_unsorted(CVType type)
{
	return type == CV_VERY || cv_compare(type);
}

int
cv_compare(CVType type)
{
	return type == CV_COMPARE || type == CV_DIFF;
}

void
update_screen(UpdateType update_kind)
{
	if(curr_stats.load_stage < 2)
		return;

	if(update_kind == UT_NONE)
		return;

	resize_all();

	if(curr_stats.restart_in_progress)
	{
		return;
	}

	update_attributes();

	if(middle_border_is_visible())
	{
		clear_border(mborder);
	}

	if(curr_stats.term_state != TS_NORMAL)
	{
		return;
	}

	curr_stats.need_update = UT_NONE;

	update_views(update_kind == UT_FULL);
	/* Redraw message dialog over updated panes.  It's not very nice to do it
	 * here, but for sure better then blocking pane updates by checking for
	 * message mode. */
	if(vle_mode_is(MSG_MODE))
	{
		redraw_msg_dialog(0);
	}

	update_stat_window(curr_view, 0);

	if(!is_status_bar_multiline())
	{
		if(curr_view->selected_files)
		{
			print_selected_msg();
		}
		else
		{
			ui_sb_clear();
		}

		if(vle_mode_is(VIEW_MODE))
		{
			view_ruler_update();
		}
		else
		{
			ui_ruler_update(curr_view, 1);
		}
	}

	if(curr_stats.save_msg == 0)
	{
		status_bar_message("");
	}

	if(vle_mode_is(VIEW_MODE) ||
			(curr_stats.number_of_windows == 2 && other_view->explore_mode))
	{
		view_redraw();
	}

	update_all_windows();

	if(!curr_view->explore_mode)
	{
		fview_cursor_redraw(curr_view);
	}

	update_input_buf();
	ui_stat_job_bar_redraw();

	curr_stats.need_update = UT_NONE;
}

/* Clears border, possibly by filling it with a pattern (depends on
 * configuration). */
static void
clear_border(WINDOW *border)
{
	int i;
	int height;

	werase(border);

	if(strcmp(cfg.border_filler, " ") == 0)
	{
		return;
	}

	height = getmaxy(border);
	for(i = 0; i < height; ++i)
	{
		mvwaddstr(border, i, 0, cfg.border_filler);
	}

	wnoutrefresh(border);
}

/* Checks whether mborder should be displayed/updated.  Returns non-zero if so,
 * otherwise zero is returned. */
static int
middle_border_is_visible(void)
{
	return (curr_stats.number_of_windows == 2 && curr_stats.split == VSPLIT);
}

/* Updates (redraws or reloads) views. */
static void
update_views(int reload)
{
	if(reload)
		reload_lists();
	else
		redraw_lists();
}

/* Reloads file lists for both views. */
static void
reload_lists(void)
{
	reload_list(curr_view);

	if(curr_stats.number_of_windows == 2)
	{
		ui_view_title_update(other_view);
		if(curr_stats.view)
		{
			qv_draw(curr_view);
		}
		else if(!other_view->explore_mode)
		{
			reload_list(other_view);
		}
	}
}

/* reloads view on window_reload() call */
static void
reload_list(FileView *view)
{
	if(curr_stats.load_stage >= 3)
		load_saving_pos(view, 1);
	else
		load_dir_list(view, !(cfg.vifm_info&VIFMINFO_SAVEDIRS) || view->list_pos != 0);
}

void
change_window(void)
{
	swap_view_roles();

	load_view_options(curr_view);

	if(window_shows_dirlist(other_view))
	{
		erase_current_line_bar(other_view);
	}

	if(curr_stats.view && !is_dir_list_loaded(curr_view))
	{
		navigate_to(curr_view, curr_view->curr_dir);
	}

	if(window_shows_dirlist(&lwin) && window_shows_dirlist(&rwin))
	{
		fview_cursor_redraw(curr_view);
		ui_views_update_titles();
	}
	else
	{
		curr_stats.need_update = UT_REDRAW;
	}
}

void
swap_view_roles(void)
{
	FileView *const tmp = curr_view;
	curr_view = other_view;
	other_view = tmp;
}

void
update_all_windows(void)
{
	if(curr_stats.load_stage >= 2)
	{
		touch_all_windows();
		doupdate();
	}
}

void
touch_all_windows(void)
{
	int in_menu;

	if(curr_stats.load_stage < 2)
	{
		return;
	}

	in_menu = is_in_menu_like_mode();

	if(!in_menu)
	{
		if(curr_stats.number_of_windows == 1)
		{
			/* In one window view. */
			update_view(curr_view);
		}
		else
		{
			/* Two pane View. */
			update_window_lazy(mborder);
			update_window_lazy(top_line);

			update_view(&lwin);
			update_view(&rwin);
		}

		if(cfg.display_statusline)
		{
			update_window_lazy(stat_win);
		}

		if(ui_stat_job_bar_height() != 0)
		{
			update_window_lazy(job_bar);
		}
	}

	update_window_lazy(ruler_win);
	update_window_lazy(input_win);
	update_window_lazy(status_bar);

	if(vle_mode_is(MSG_MODE))
	{
		/* Location of message dialog can change, so it's better to let it position
		 * itself or multiple copies appear on the screen. */
		redraw_msg_dialog(1);
	}
}

/* Updates all parts of file view. */
static void
update_view(FileView *view)
{
	update_window_lazy(view->title);

	/* If view displays graphics, we don't want to update it or the image will be
	 * lost. */
	if(!view->explore_mode && !(curr_stats.view && view == other_view))
	{
		update_window_lazy(view->win);
	}
}

/* Tell curses to internally mark window as changed. */
static void
update_window_lazy(WINDOW *win)
{
	touchwin(win);
	/*
	 * redrawwin() shouldn't be needed.  But without it there is a
	 * lot of flickering when redrawing the windows?
	 */
	redrawwin(win);
	wnoutrefresh(win);
}

void
update_input_bar(const wchar_t *str)
{
	if(!curr_stats.use_input_bar)
		return;

	if(wcslen(str) > (size_t)getmaxx(input_win))
	{
		str += wcslen(str) - getmaxx(input_win);
	}

	werase(input_win);
	compat_waddwstr(input_win, str);
	wrefresh(input_win);
}

void
clear_num_window(void)
{
	if(curr_stats.use_input_bar)
	{
		werase(input_win);
		wrefresh(input_win);
	}
}

void
show_progress(const char msg[], int period)
{
	static char marks[] = { '|', '/', '-', '\\' };
	static int mark = 0;
	static int count = 1;
	static int total = 0;

	/* Do nothing if UI is not functional. */
	if(curr_stats.load_stage < 1)
	{
		return;
	}

	/* Reset state. */
	if(period == 0)
	{
		count = 1;
		total = 0;
		return;
	}

	/* Advance state. */
	++count;
	++total;

	/* Skip intermediate updates to do not hammer UI with refreshes. */
	if(abs(period) != 1 && count%abs(period) != 1)
	{
		return;
	}
	count = 1;

	/* Assume that period equal to or less than one means that message already
	 * contains counter (maybe along with total number) or doesn't need one. */
	if(period <= 1)
	{
		ui_sb_quick_msgf("%s %c", msg, marks[mark]);
	}
	else
	{
		ui_sb_quick_msgf("%s %c %d", msg, marks[mark], total);
	}

	/* Pick next mark character. */
	mark = (mark + 1) % sizeof(marks);
}

void
redraw_lists(void)
{
	redraw_current_view();
	if(curr_stats.number_of_windows == 2)
	{
		if(curr_stats.view)
		{
			qv_draw(curr_view);
			refresh_view_win(other_view);
		}
		else if(!other_view->explore_mode)
		{
			fview_cursor_redraw(other_view);
			draw_dir_list(other_view);
			refresh_view_win(other_view);
		}
	}
}

void
update_attributes(void)
{
	int attr;

	if(curr_stats.load_stage < 2)
		return;

	attr = cfg.cs.color[BORDER_COLOR].attr;
	wbkgdset(mborder, COLOR_PAIR(cfg.cs.pair[BORDER_COLOR]) | attr);
	werase(mborder);

	wbkgdset(top_line, COLOR_PAIR(cfg.cs.pair[TOP_LINE_COLOR]) | (cfg.cs.color[TOP_LINE_COLOR].attr & A_REVERSE));
	wattrset(top_line, cfg.cs.color[TOP_LINE_COLOR].attr & ~A_REVERSE);
	werase(top_line);

	attr = cfg.cs.color[STATUS_LINE_COLOR].attr;
	wbkgdset(stat_win, COLOR_PAIR(cfg.cs.pair[STATUS_LINE_COLOR]) | attr);

	attr = cfg.cs.color[JOB_LINE_COLOR].attr;
	wbkgdset(job_bar, COLOR_PAIR(cfg.cs.pair[JOB_LINE_COLOR]) | attr);

	attr = cfg.cs.color[WIN_COLOR].attr;
	wbkgdset(menu_win, COLOR_PAIR(cfg.cs.pair[WIN_COLOR]) | attr);
	wbkgdset(sort_win, COLOR_PAIR(cfg.cs.pair[WIN_COLOR]) | attr);
	wbkgdset(change_win, COLOR_PAIR(cfg.cs.pair[WIN_COLOR]) | attr);
	wbkgdset(error_win, COLOR_PAIR(cfg.cs.pair[WIN_COLOR]) | attr);

	wattrset(status_bar, cfg.cs.color[CMD_LINE_COLOR].attr);
	wbkgdset(status_bar, COLOR_PAIR(cfg.cs.pair[CMD_LINE_COLOR]));

	wattrset(ruler_win, cfg.cs.color[CMD_LINE_COLOR].attr);
	wbkgdset(ruler_win, COLOR_PAIR(cfg.cs.pair[CMD_LINE_COLOR]));

	wattrset(input_win, cfg.cs.color[CMD_LINE_COLOR].attr);
	wbkgdset(input_win, COLOR_PAIR(cfg.cs.pair[CMD_LINE_COLOR]));
}

void
wprint(WINDOW *win, const char str[])
{
#ifndef _WIN32
	waddstr(win, str);
#else
	wchar_t *t = to_wide(str);
	if(t == NULL)
	{
		show_error_msg("Memory Error", "Unable to allocate enough memory");
		return;
	}

	compat_waddwstr(win, t);
	free(t);
#endif
}

void
wprinta(WINDOW *win, const char str[], int line_attrs)
{
	wattron(win, line_attrs);
	wprint(win, str);
	wattroff(win, line_attrs);
	wnoutrefresh(win);
}

int
resize_for_menu_like(void)
{
	int screen_x, screen_y;

	ui_update_term_state();
	if(curr_stats.term_state == TS_TOO_SMALL)
	{
		return 1;
	}

	update_term_size();
	flushinp(); /* without it we will get strange character on input */
	getmaxyx(stdscr, screen_y, screen_x);

	werase(stdscr);
	werase(status_bar);
	werase(ruler_win);

	wresize(menu_win, screen_y - 1, screen_x);

	update_statusbar_layout();

	wrefresh(status_bar);
	wrefresh(ruler_win);
	wrefresh(input_win);

	return 0;
}

/* Query terminal size from the "device" and pass it to curses library. */
static void
update_term_size(void)
{
#ifndef _WIN32
	struct winsize ws = { .ws_col = -1, .ws_row = -1 };

	if(ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == -1)
	{
		LOG_SERROR_MSG(errno, "Failed to query terminal size.");
		vifm_finish("Terminal error.");
	}
	/* Allow 0 as GNU Hurd returns this, is_term_resized() returns false then, but
	 * everything works. */
	if(ws.ws_row == (typeof(ws.ws_row))-1 || ws.ws_col == (typeof(ws.ws_col))-1)
	{
		LOG_INFO_MSG("ws.ws_row = %d; ws.ws_col = %d", ws.ws_row, ws.ws_col);
		vifm_finish("Terminal is unable to run vifm.");
	}

	if(is_term_resized(ws.ws_row, ws.ws_col))
	{
		resizeterm(ws.ws_row, ws.ws_col);
	}
#endif
}

/* Re-layouts windows located on status bar (status bar itself, input and the
 * ruler). */
static void
update_statusbar_layout(void)
{
	int screen_x, screen_y;

	int ruler_width;
	int fields_pos;

	getmaxyx(stdscr, screen_y, screen_x);

	ruler_width = get_ruler_width(curr_view);
	fields_pos = screen_x - (INPUT_WIN_WIDTH + ruler_width);

	wresize(status_bar, 1, fields_pos);
	mvwin(status_bar, screen_y - 1, 0);

	wresize(ruler_win, 1, ruler_width);
	mvwin(ruler_win, screen_y - 1, fields_pos + INPUT_WIN_WIDTH);
	wnoutrefresh(ruler_win);

	wresize(input_win, 1, INPUT_WIN_WIDTH);
	mvwin(input_win, screen_y - 1, fields_pos);
	wnoutrefresh(input_win);
}

/* Gets "recommended" width for the ruler.  Returns the width. */
static int
get_ruler_width(FileView *view)
{
	char *expanded;
	int len;
	int list_pos;

	/* Size must correspond to the "worst case" of the last list item. */
	list_pos = view->list_pos;
	view->list_pos = (view->list_rows == 0) ? 0 : (view->list_rows - 1);

	expanded = expand_ruler_macros(view, cfg.ruler_format);
	len = strlen(expanded);
	free(expanded);

	view->list_pos = list_pos;

	return MAX(POS_WIN_MIN_WIDTH, len);
}

/* Expands view macros to be displayed on the ruler line according to the format
 * string.  Returns newly allocated string, which should be freed by the caller,
 * or NULL if there is not enough memory. */
static char *
expand_ruler_macros(FileView *view, const char format[])
{
	return expand_view_macros(view, format, "-lLS%[]");
}

void
refresh_view_win(FileView *view)
{
	if(curr_stats.restart_in_progress)
	{
		return;
	}

	wrefresh(view->win);
	/* Use getmaxy(...) instead of multiline_status_bar to handle command line
	 * mode, which doesn't use this module to show multiline messages. */
	if(cfg.display_statusline && getmaxy(status_bar) > 1)
	{
		touchwin(stat_win);
		wrefresh(stat_win);
	}
}

void
move_window(FileView *view, int horizontally, int first)
{
	const SPLIT split_type = horizontally ? HSPLIT : VSPLIT;
	const FileView *const desired_view = first ? &lwin : &rwin;
	split_view(split_type);
	if(view != desired_view)
	{
		/* Switch two panes saving current windows as the active one (left/top or
		 * right/bottom). */
		switch_panes_content();
		go_to_other_pane();
	}
}

void
switch_panes(void)
{
	switch_panes_content();
	try_activate_view_mode();
}

void
ui_view_pick(FileView *view, FileView **old_curr, FileView **old_other)
{
	*old_curr = curr_view;
	*old_other = other_view;

	curr_view = view;
	other_view = (view == *old_curr) ? *old_other : *old_curr;
	if(curr_view != *old_curr)
	{
		load_view_options(curr_view);
	}
}

void
ui_view_unpick(FileView *view, FileView *old_curr, FileView *old_other)
{
	if(curr_view != view)
	{
		/* Do nothing if view roles were switched from outside. */
		return;
	}

	curr_view = old_curr;
	other_view = old_other;
	if(curr_view != view)
	{
		load_view_options(curr_view);
	}
}

/* Switches panes content. */
static void
switch_panes_content(void)
{
	FileView tmp_view;
	WINDOW* tmp;
	int t;

	if(!vle_mode_is(VIEW_MODE))
	{
		view_switch_views();
	}

	tmp = lwin.win;
	lwin.win = rwin.win;
	rwin.win = tmp;

	t = lwin.window_rows;
	lwin.window_rows = rwin.window_rows;
	rwin.window_rows = t;

	t = lwin.window_width;
	lwin.window_width = rwin.window_width;
	rwin.window_width = t;

	t = lwin.local_cs;
	lwin.local_cs = rwin.local_cs;
	rwin.local_cs = t;

	tmp = lwin.title;
	lwin.title = rwin.title;
	rwin.title = tmp;

	tmp_view = lwin;
	lwin = rwin;
	rwin = tmp_view;

	update_origins(&lwin, &rwin.curr_dir[0]);
	update_origins(&rwin, &lwin.curr_dir[0]);

	curr_stats.need_update = UT_REDRAW;
}

/* Updates pointers to main (default) origins in file list entries. */
static void
update_origins(FileView *view, const char *old_main_origin)
{
	char *const new_origin = &view->curr_dir[0];
	int i;
	for(i = 0; i < view->list_rows; ++i)
	{
		dir_entry_t *const entry = &view->dir_entry[i];
		if(entry->origin == old_main_origin)
		{
			entry->origin = new_origin;
		}
	}
}

void
go_to_other_pane(void)
{
	change_window();
	try_activate_view_mode();
}

void
split_view(SPLIT orientation)
{
	if(curr_stats.number_of_windows == 2 && curr_stats.split == orientation)
		return;

	if(curr_stats.number_of_windows == 2 && curr_stats.splitter_pos > 0)
	{
		if(orientation == VSPLIT)
			curr_stats.splitter_pos *= (float)getmaxx(stdscr)/getmaxy(stdscr);
		else
			curr_stats.splitter_pos *= (float)getmaxy(stdscr)/getmaxx(stdscr);
	}

	curr_stats.split = orientation;
	curr_stats.number_of_windows = 2;
	curr_stats.need_update = UT_REDRAW;
}

void
only(void)
{
	if(curr_stats.number_of_windows != 1)
	{
		curr_stats.number_of_windows = 1;
		update_screen(UT_REDRAW);
	}
}

void
move_splitter(int by, int fact)
{
	/* Determine exact splitter position if it's centered at the moment. */
	if(curr_stats.splitter_pos < 0)
	{
		if(curr_stats.split == VSPLIT)
		{
			curr_stats.splitter_pos = getmaxx(stdscr)/2 - 1 + getmaxx(stdscr)%2;
		}
		else
		{
			curr_stats.splitter_pos = getmaxy(stdscr)/2 - 1;
		}
	}

	set_splitter(curr_stats.splitter_pos + fact*by);
}

void
ui_view_resize(FileView *view, int to)
{
	int pos;

	if(curr_stats.split == HSPLIT)
	{
		const int height = get_working_area_height();
		pos = (view == &lwin) ? (1 + to) : (height - to);
	}
	else
	{
		const int width = getmaxx(stdscr) - 1;
		pos = (view == &lwin) ? to : (width - to);
	}

	set_splitter(pos);
}

/* Sets splitter position making sure it isn't negative value which has special
 * meaning. */
static void
set_splitter(int pos)
{
	curr_stats.splitter_pos = (pos < 0) ? 0 : pos;
	update_screen(UT_REDRAW);
}

void
format_entry_name(const dir_entry_t *entry, size_t buf_len, char buf[])
{
	const char *prefix, *suffix;
	ui_get_decors(entry, &prefix, &suffix);
	snprintf(buf, buf_len, "%s%s%s", prefix, entry->name, suffix);
}

void
ui_get_decors(const dir_entry_t *entry, const char **prefix,
		const char **suffix)
{
	/* The check of actual file type can be relatively slow in some cases, so make
	 * sure we do it only when needed and at most once. */
	FileType type = FT_UNK;

	if(entry->name_dec_num == -1)
	{
		/* Find a match and cache the result. */

		((dir_entry_t *)entry)->name_dec_num = 0;
		if(cfg.name_dec_count != 0)
		{
			char full_path[PATH_MAX];
			int i;

			get_full_path_of(entry, sizeof(full_path) - 1U, full_path);
			type = ui_view_entry_target_type(entry);
			if(type == FT_DIR)
			{
				strcat(full_path, "/");
			}

			for(i = 0; i < cfg.name_dec_count; ++i)
			{
				const file_dec_t *const file_dec = &cfg.name_decs[i];
				if(matchers_match(file_dec->matchers, full_path))
				{
					((dir_entry_t *)entry)->name_dec_num = i + 1;
					break;
				}
			}
		}
	}

	if(entry->name_dec_num == 0)
	{
		type = (type == FT_UNK) ? ui_view_entry_target_type(entry) : type;
		*prefix = cfg.type_decs[type][DECORATION_PREFIX];
		*suffix = cfg.type_decs[type][DECORATION_SUFFIX];
	}
	else
	{
		assert(entry->name_dec_num - 1 >= 0 && "Wrong index.");
		assert(entry->name_dec_num - 1 < cfg.name_dec_count && "Wrong index.");

		*prefix = cfg.name_decs[entry->name_dec_num - 1].prefix;
		*suffix = cfg.name_decs[entry->name_dec_num - 1].suffix;
	}
}

void
checked_wmove(WINDOW *win, int y, int x)
{
	if(wmove(win, y, x) == ERR)
	{
		LOG_INFO_MSG("Error moving cursor on a window to (x=%d, y=%d).", x, y);
	}
}

void
ui_display_too_small_term_msg(void)
{
	touchwin(stdscr);
	wrefresh(stdscr);

	mvwin(status_bar, 0, 0);
	wresize(status_bar, getmaxy(stdscr), getmaxx(stdscr));
	werase(status_bar);
	waddstr(status_bar, "Terminal is too small for vifm");
	touchwin(status_bar);
	wrefresh(status_bar);
}

void
ui_view_win_changed(FileView *view)
{
	wnoutrefresh(view->win);
}

void
ui_view_reset_selection_and_reload(FileView *view)
{
	flist_sel_stash(view);
	load_saving_pos(view, 1);
}

void
ui_view_reset_search_highlight(FileView *view)
{
	if(view->matches != 0)
	{
		view->matches = 0;
		ui_view_schedule_redraw(view);
	}
}

void
ui_views_reload_visible_filelists(void)
{
	if(curr_stats.view)
	{
		load_saving_pos(curr_view, 1);
	}
	else
	{
		ui_views_reload_filelists();
	}
}

void
ui_views_reload_filelists(void)
{
	load_saving_pos(curr_view, 1);
	load_saving_pos(other_view, 1);
}

void
ui_views_update_titles(void)
{
	ui_view_title_update(&lwin);
	ui_view_title_update(&rwin);
}

void
ui_view_title_update(FileView *view)
{
	char *title;
	const int gen_view = vle_mode_is(VIEW_MODE) && !curr_view->explore_mode;
	FileView *selected = gen_view ? other_view : curr_view;

	if(curr_stats.load_stage < 2)
	{
		return;
	}

	title = format_view_title(view, cfg.shorten_title_paths ? &replace_home_part : &path_identity);

	if(view == selected && cfg.set_title)
	{
		term_title_update(title);
	}

	print_view_title(view, view == selected, title);

	wnoutrefresh(view->title);

	free(title);
}

/* Identity path function.  Returns its argument. */
static char *
path_identity(const char path[])
{
	return (char *)path;
}

/* Formats title for the view.  The pf function will be applied to full paths.
 * Returns newly allocated string, which should be freed by the caller, or NULL
 * if there is not enough memory. */
static char *
format_view_title(const FileView *view, path_func pf)
{
	if(view->explore_mode)
	{
		char full_path[PATH_MAX];
		get_current_full_path(view, sizeof(full_path), full_path);
		return strdup(pf(full_path));
	}
	else if(curr_stats.view && view == other_view)
	{
		return format_str("File: %s", get_current_file_name(curr_view));
	}
	else if(flist_custom_active(view))
	{
		return format_str("[%s] @ %s", view->custom.title, pf(view->custom.orig_dir));
	}
	else
	{
		return strdup(pf(view->curr_dir));
	}
}

/* Prints view title (which can be changed for printing).  Takes care of setting
 * correct attributes. */
static void
print_view_title(const FileView *view, int active_view, char title[])
{
	const size_t title_width = getmaxx(view->title);
	if(title_width == (size_t)-1)
	{
		return;
	}

	fixup_titles_attributes(view, active_view);
	werase(view->title);

	if(active_view)
	{
		wprint(view->title, left_ellipsis(title, title_width));
	}
	else
	{
		wprint(view->title, right_ellipsis(title, title_width));
	}
}

/* Updates attributes for view titles and top line. */
static void
fixup_titles_attributes(const FileView *view, int active_view)
{
	if(active_view)
	{
		col_attr_t col = cfg.cs.color[TOP_LINE_COLOR];
		cs_mix_colors(&col, &cfg.cs.color[TOP_LINE_SEL_COLOR]);

		wbkgdset(view->title, COLOR_PAIR(colmgr_get_pair(col.fg, col.bg)) | (col.attr & A_REVERSE));
		wattrset(view->title, col.attr & ~A_REVERSE);
	}
	else
	{
		col_attr_t col = cfg.cs.color[TOP_LINE_COLOR];
		const int bg_attr = COLOR_PAIR(cfg.cs.pair[TOP_LINE_COLOR]) | (col.attr & A_REVERSE);

		wbkgdset(view->title, bg_attr);
		wattrset(view->title, col.attr & ~A_REVERSE);
		wbkgdset(top_line, bg_attr);
		wattrset(top_line, col.attr & ~A_REVERSE);
		werase(top_line);
	}
}

int
ui_view_sort_list_contains(const char sort[SK_COUNT], char key)
{
	int i = -1;
	while(++i < SK_COUNT)
	{
		const int sort_key = abs(sort[i]);
		if(sort_key > SK_LAST)
		{
			return 0;
		}
		else if(sort_key == key)
		{
			return 1;
		}
	}
	return 0;
}

void
ui_view_sort_list_ensure_well_formed(FileView *view, char sort_keys[])
{
	int found_name_key = 0;
	int i = -1;

	while(++i < SK_COUNT)
	{
		const int sort_key = abs(sort_keys[i]);
		if(sort_key > SK_LAST)
		{
			break;
		}
		else if(sort_key == SK_BY_NAME || sort_key == SK_BY_INAME)
		{
			found_name_key = 1;
		}
	}

	if(!found_name_key && i < SK_COUNT &&
			(!flist_custom_active(view) ||
			 (sort_keys == view->sort && !ui_view_unsorted(view))))
	{
		sort_keys[i++] = SK_DEFAULT;
	}

	if(i < SK_COUNT)
	{
		memset(&sort_keys[i], SK_NONE, SK_COUNT - i);
	}
}

char *
ui_view_sort_list_get(const FileView *view, const char sort[])
{
	return (flist_custom_active(view) && ui_view_unsorted(view))
	     ? (char *)view->custom.sort
	     : (char *)sort;
}

int
ui_view_displays_numbers(const FileView *const view)
{
	return view->num_type != NT_NONE && ui_view_displays_columns(view);
}

int
ui_view_is_visible(const FileView *const view)
{
	return curr_stats.number_of_windows == 2 || curr_view == view;
}

void
ui_view_clear_history(FileView *const view)
{
	cfg_free_history_items(view->history, view->history_num);
	view->history_num = 0;
	view->history_pos = 0;
}

int
ui_view_displays_columns(const FileView *const view)
{
	return !view->ls_view
	    || (flist_custom_active(view) &&
					(view->custom.type == CV_TREE || cv_compare(view->custom.type)));
}

FileType
ui_view_entry_target_type(const dir_entry_t *entry)
{
	if(entry->type == FT_LINK)
	{
		char *const full_path = format_str("%s/%s", entry->origin, entry->name);
		const FileType type = (get_symlink_type(full_path) != SLT_UNKNOWN) ? FT_DIR : FT_LINK;

		free(full_path);
		return type;
	}

	return entry->type;
}

int
ui_view_available_width(const FileView *const view)
{
	const int correction = cfg.extra_padding ? -2 : 0;
	return ((int)view->window_width + 1) + correction;
}

int
ui_qv_left(const FileView *view)
{
	return cfg.extra_padding ? 1 : 0;
}

int
ui_qv_top(const FileView *view)
{
	return cfg.extra_padding ? 1 : 0;
}

int
ui_qv_height(const FileView *view)
{
	return cfg.extra_padding ? view->window_rows - 1 : view->window_rows + 1;
}

int
ui_qv_width(const FileView *view)
{
	return cfg.extra_padding ? view->window_width - 1 : view->window_width + 1;
}

const col_scheme_t *
ui_view_get_cs(const FileView *view)
{
	return view->local_cs ? &view->cs : &cfg.cs;
}

void
ui_view_erase(FileView *view)
{
	const col_scheme_t *cs = ui_view_get_cs(view);
	const int bg = COLOR_PAIR(cs->pair[WIN_COLOR]) | cs->color[WIN_COLOR].attr;
	wbkgdset(view->win, bg);
	werase(view->win);
}

void
ui_view_wipe(FileView *view)
{
	int i;
	int height;
	short int fg, bg;
	char line_filler[getmaxx(view->win) + 1];

	line_filler[sizeof(line_filler) - 1] = '\0';
	height = getmaxy(view->win);

	/* User doesn't need to see fake filling so draw it with the color of
	 * background. */
	(void)pair_content(PAIR_NUMBER(getbkgd(view->win)), &fg, &bg);
	wattrset(view->win, COLOR_PAIR(colmgr_get_pair(bg, bg)));

	memset(line_filler, '\t', sizeof(line_filler));
	for(i = 0; i < height; ++i)
	{
		mvwaddstr(view->win, i, 0, line_filler);
	}
	redrawwin(view->win);
	wrefresh(view->win);
}

int
ui_view_unsorted(const FileView *view)
{
	return cv_unsorted(view->custom.type);
}

void
ui_view_schedule_redraw(FileView *view)
{
	pthread_mutex_lock(view->timestamps_mutex);
	view->postponed_redraw = get_updated_time(view->postponed_redraw);
	pthread_mutex_unlock(view->timestamps_mutex);
}

void
ui_view_schedule_reload(FileView *view)
{
	pthread_mutex_lock(view->timestamps_mutex);
	view->postponed_reload = get_updated_time(view->postponed_reload);
	pthread_mutex_unlock(view->timestamps_mutex);
}

void
ui_view_schedule_full_reload(FileView *view)
{
	pthread_mutex_lock(view->timestamps_mutex);
	view->postponed_full_reload = get_updated_time(view->postponed_full_reload);
	pthread_mutex_unlock(view->timestamps_mutex);
}

/* Gets updated timestamp ensuring that it differs from the previous value.
 * Returns the timestamp. */
static uint64_t
get_updated_time(uint64_t prev)
{
	struct timeval tv = {0};
	uint64_t new;

	(void)gettimeofday(&tv, NULL);

	new = tv.tv_sec*1000000ULL + tv.tv_usec;
	if(new == prev)
	{
		++new;
	}

	return new;
}

UiUpdateEvent
ui_view_query_scheduled_event(FileView *view)
{
	UiUpdateEvent event;

	pthread_mutex_lock(view->timestamps_mutex);

	if(view->postponed_full_reload != view->last_reload)
	{
		event = UUE_FULL_RELOAD;
	}
	else if(view->postponed_reload != view->last_reload)
	{
		event = UUE_RELOAD;
	}
	else if(view->postponed_redraw != view->last_redraw)
	{
		event = UUE_REDRAW;
	}
	else
	{
		event = UUE_NONE;
	}

	view->last_redraw = view->postponed_redraw;
	view->last_reload = view->postponed_reload;
	view->postponed_full_reload = view->postponed_reload;

	pthread_mutex_unlock(view->timestamps_mutex);

	return event;
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
