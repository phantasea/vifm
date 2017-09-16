#include "utils.h"

#include <sys/stat.h> /* chmod() */
#include <unistd.h> /* access() */

#include <stddef.h> /* NULL */
#include <stdio.h> /* FILE fclose() fopen() fread() */
#include <stdlib.h> /* free() */
#include <string.h> /* memset() strcpy() strdup() */

#include "../../src/cfg/config.h"
#include "../../src/compat/os.h"
#include "../../src/engine/options.h"
#include "../../src/ui/ui.h"
#include "../../src/utils/dynarray.h"
#include "../../src/utils/fswatch.h"
#include "../../src/utils/matcher.h"
#include "../../src/utils/path.h"
#include "../../src/utils/str.h"
#include "../../src/filelist.h"
#include "../../src/filtering.h"
#include "../../src/opt_handlers.h"
#include "../../src/undo.h"

static int exec_func(OPS op, void *data, const char *src, const char *dst);
static int op_avail(OPS op);
static void init_list(view_t *view);

void
opt_handlers_setup(void)
{
	update_string(&lwin.view_columns, "");
	update_string(&lwin.view_columns_g, "");
	update_string(&lwin.sort_groups, "");
	update_string(&lwin.sort_groups_g, "");
	update_string(&lwin.preview_prg, "");
	update_string(&lwin.preview_prg_g, "");
	update_string(&rwin.view_columns, "");
	update_string(&rwin.view_columns_g, "");
	update_string(&rwin.sort_groups, "");
	update_string(&rwin.sort_groups_g, "");
	update_string(&rwin.preview_prg, "");
	update_string(&rwin.preview_prg_g, "");

	update_string(&cfg.slow_fs_list, "");
	update_string(&cfg.apropos_prg, "");
	update_string(&cfg.cd_path, "");
	update_string(&cfg.find_prg, "");
	update_string(&cfg.fuse_home, "");
	update_string(&cfg.time_format, "+");
	update_string(&cfg.vi_command, "");
	update_string(&cfg.vi_x_command, "");
	update_string(&cfg.ruler_format, "");
	update_string(&cfg.status_line, "");
	update_string(&cfg.grep_prg, "");
	update_string(&cfg.locate_prg, "");
	update_string(&cfg.border_filler, "");
	update_string(&cfg.shell, "");

	init_option_handlers();
}

void
opt_handlers_teardown(void)
{
	clear_options();

	update_string(&cfg.slow_fs_list, NULL);
	update_string(&cfg.apropos_prg, NULL);
	update_string(&cfg.cd_path, NULL);
	update_string(&cfg.find_prg, NULL);
	update_string(&cfg.fuse_home, NULL);
	update_string(&cfg.time_format, NULL);
	update_string(&cfg.vi_command, NULL);
	update_string(&cfg.vi_x_command, NULL);
	update_string(&cfg.ruler_format, NULL);
	update_string(&cfg.status_line, NULL);
	update_string(&cfg.grep_prg, NULL);
	update_string(&cfg.locate_prg, NULL);
	update_string(&cfg.border_filler, NULL);
	update_string(&cfg.shell, NULL);

	update_string(&lwin.view_columns, NULL);
	update_string(&lwin.view_columns_g, NULL);
	update_string(&lwin.sort_groups, NULL);
	update_string(&lwin.sort_groups_g, NULL);
	update_string(&lwin.preview_prg, NULL);
	update_string(&lwin.preview_prg_g, NULL);
	update_string(&rwin.view_columns, NULL);
	update_string(&rwin.view_columns_g, NULL);
	update_string(&rwin.sort_groups, NULL);
	update_string(&rwin.sort_groups_g, NULL);
	update_string(&rwin.preview_prg, NULL);
	update_string(&rwin.preview_prg_g, NULL);
}

void
undo_setup(void)
{
	static int max_undo_levels = 0;
	init_undo_list(&exec_func, &op_avail, NULL, &max_undo_levels);
}

static int
exec_func(OPS op, void *data, const char *src, const char *dst)
{
	return 0;
}

static int
op_avail(OPS op)
{
	return 0;
}

void
undo_teardown(void)
{
	reset_undo_list();
}

void
view_setup(view_t *view)
{
	char *error;

	view->list_rows = 0;
	view->filtered = 0;
	view->list_pos = 0;
	view->dir_entry = NULL;
	view->hide_dot = 0;
	view->hide_dot_g = 0;
	view->invert = 1;
	view->selected_files = 0;

	assert_success(filter_init(&view->local_filter.filter, 1));
	assert_non_null(view->manual_filter = matcher_alloc("", 0, 0, "", &error));
	assert_success(filter_init(&view->auto_filter, 1));

	strcpy(view->curr_dir, "/path");
	update_string(&view->custom.orig_dir, NULL);

	view->sort[0] = SK_BY_NAME;
	memset(&view->sort[1], SK_NONE, sizeof(view->sort) - 1);

	view->custom.entry_count = 0;
	view->custom.entries = NULL;
}

void
view_teardown(view_t *view)
{
	int i;

	for(i = 0; i < view->list_rows; ++i)
	{
		fentry_free(view, &view->dir_entry[i]);
	}
	dynarray_free(view->dir_entry);

	for(i = 0; i < view->custom.entry_count; ++i)
	{
		fentry_free(view, &view->custom.entries[i]);
	}
	dynarray_free(view->custom.entries);

	filter_dispose(&view->local_filter.filter);
	filter_dispose(&view->auto_filter);
	matcher_free(view->manual_filter);
	view->manual_filter = NULL;

	view->custom.type = CV_REGULAR;

	fswatch_free(view->watch);
	view->watch = NULL;
}

void
create_file(const char path[])
{
	FILE *const f = fopen(path, "w");
	assert_non_null(f);
	if(f != NULL)
	{
		fclose(f);
	}
}

void
create_executable(const char path[])
{
	create_file(path);
	assert_success(access(path, F_OK));
	chmod(path, 0755);
	assert_success(access(path, X_OK));
}

void
make_abs_path(char buf[], size_t buf_len, const char base[], const char sub[],
		const char cwd[])
{
	char local_buf[buf_len];

	if(is_path_absolute(base))
	{
		snprintf(local_buf, buf_len, "%s%s%s", base, (sub[0] == '\0' ? "" : "/"),
				sub);
	}
	else
	{
		snprintf(local_buf, buf_len, "%s/%s%s%s", cwd, base,
				(sub[0] == '\0' ? "" : "/"), sub);
	}

	canonicalize_path(local_buf, buf, buf_len);
	if(!ends_with_slash(sub) && !is_root_dir(buf))
	{
		chosp(buf);
	}
}

void
copy_file(const char src[], const char dst[])
{
	char buf[4*1024];
	size_t nread;
	FILE *const in = os_fopen(src, "rb");
	FILE *const out = os_fopen(dst, "wb");

	assert_non_null(in);
	assert_non_null(out);

	while((nread = fread(&buf, 1, sizeof(buf), in)) != 0U)
	{
		assert_int_equal(nread, fwrite(&buf, 1, nread, out));
	}

	fclose(out);
	fclose(in);
}

int
not_windows(void)
{
#ifdef _WIN32
	return 0;
#else
	return 1;
#endif
}

int
replace_matcher(matcher_t **matcher, const char expr[])
{
	char *error;

	matcher_free(*matcher);
	*matcher = matcher_alloc(expr, FILTER_DEF_CASE_SENSITIVITY, 0, "", &error);
	free(error);

	return (*matcher == NULL);
}

void
setup_grid(view_t *view, int column_count, int list_rows, int init)
{
	view->ls_view = 1;
	view->ls_transposed = 0;
	view->list_rows = list_rows;
	view->column_count = column_count;
	view->run_size = column_count;
	view->window_cells = column_count*view->window_rows;

	if(init)
	{
		init_list(view);
	}
}

void
setup_transposed_grid(view_t *view, int column_count, int list_rows, int init)
{
	view->ls_view = 1;
	view->ls_transposed = 1;
	view->list_rows = list_rows;
	view->column_count = column_count;
	view->run_size = view->window_rows;
	view->window_cells = column_count*view->window_rows;

	if(init)
	{
		init_list(view);
	}
}

static void
init_list(view_t *view)
{
	int i;

	view->dir_entry = dynarray_cextend(NULL,
			view->list_rows*sizeof(*view->dir_entry));

	for(i = 0; i < view->list_rows; ++i)
	{
		view->dir_entry[i].name = strdup("");
		view->dir_entry[i].type = FT_REG;
		view->dir_entry[i].origin = view->curr_dir;
	}
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
