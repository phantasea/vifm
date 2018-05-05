#include <stic.h>

#include <curses.h> /* COLORS */

#include "../../src/cfg/config.h"
#include "../../src/ui/escape.h"
#include "../../src/utils/utils.h"

#include "utils.h"

static esc_state state;

SETUP()
{
	col_attr_t def_color = { .fg = 1, .bg = 2, .attr = 0 };
	esc_state_init(&state, &def_color);
}

TEST(color_palette_256_is_supported)
{
	int colors = COLORS;
	COLORS = 256;

	esc_state_update(&state, "\033[38;5;123m");
	assert_int_equal(123, state.fg);

	esc_state_update(&state, "\033[48;5;213m");
	assert_int_equal(213, state.bg);

	COLORS = colors;
}

TEST(resetting_things_work)
{
	state.attrs = A_DIM;
	state.fg = 123;
	state.bg = 132;

	esc_state_update(&state, "\033[m");
	assert_int_equal(1, state.fg);
	assert_int_equal(2, state.bg);
	assert_int_equal(0, state.attrs);

	state.attrs = A_BOLD | A_UNDERLINE | A_BLINK | A_REVERSE | A_DIM;
	esc_state_update(&state, "\033[22m");
	assert_int_equal(0, state.attrs);
}

TEST(bold_and_dim_are_parsed)
{
	esc_state_update(&state, "\033[1m");
	assert_int_equal(A_BOLD, state.attrs);
	esc_state_update(&state, "\033[2m");
	assert_int_equal(A_BOLD | A_DIM, state.attrs);
}

TEST(italic_is_parsed)
{
#ifdef HAVE_A_ITALIC_DECL
	const int italic_attr = A_ITALIC;
#else
	const int italic_attr = A_REVERSE;
#endif

	esc_state_update(&state, "\033[3m");
	assert_int_equal(italic_attr, state.attrs);
	esc_state_update(&state, "\033[23m");
	assert_int_equal(0, state.attrs);
}

TEST(underline_is_parsed)
{
	esc_state_update(&state, "\033[4m");
	assert_int_equal(A_UNDERLINE, state.attrs);
	esc_state_update(&state, "\033[24m");
	assert_int_equal(0, state.attrs);
}

TEST(blink_is_parsed)
{
	esc_state_update(&state, "\033[5m");
	assert_int_equal(A_BLINK, state.attrs);
	esc_state_update(&state, "\033[25m");
	assert_int_equal(0, state.attrs);

	esc_state_update(&state, "\033[6m");
	assert_int_equal(A_BLINK, state.attrs);
	esc_state_update(&state, "\033[25m");
	assert_int_equal(0, state.attrs);
}

TEST(reverse_is_parsed)
{
	esc_state_update(&state, "\033[7m");
	assert_int_equal(A_REVERSE, state.attrs);
	esc_state_update(&state, "\033[27m");
	assert_int_equal(0, state.attrs);
}

TEST(colors_are_parsed)
{
	esc_state_update(&state, "\033[30;40m");
	assert_int_equal(0, state.fg);
	assert_int_equal(0, state.bg);
	esc_state_update(&state, "\033[31;41m");
	assert_int_equal(1, state.fg);
	assert_int_equal(1, state.bg);
	esc_state_update(&state, "\033[37;47m");
	assert_int_equal(7, state.fg);
	assert_int_equal(7, state.bg);

	esc_state_update(&state, "\033[39;49m");
	assert_int_equal(-1, state.fg);
	assert_int_equal(-1, state.bg);
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
