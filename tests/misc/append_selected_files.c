#include <stic.h>

#include <stdlib.h>
#include <string.h>

#include "../../src/cfg/config.h"
#include "../../src/ui/ui.h"
#include "../../src/utils/dynarray.h"
#include "../../src/utils/str.h"
#include "../../src/macros.h"

#include "utils.h"

SETUP()
{
	update_string(&cfg.shell, "sh");

	/* lwin */
	strcpy(lwin.curr_dir, "/lwin");

	lwin.list_rows = 4;
	lwin.list_pos = 2;
	lwin.dir_entry = dynarray_cextend(NULL,
			lwin.list_rows*sizeof(*lwin.dir_entry));
	lwin.dir_entry[0].name = strdup("lfile0");
	lwin.dir_entry[0].origin = &lwin.curr_dir[0];
	lwin.dir_entry[1].name = strdup("lfile1");
	lwin.dir_entry[1].origin = &lwin.curr_dir[0];
	lwin.dir_entry[2].name = strdup("lfile2");
	lwin.dir_entry[2].origin = &lwin.curr_dir[0];
	lwin.dir_entry[3].name = strdup("lfile3");
	lwin.dir_entry[3].origin = &lwin.curr_dir[0];

	lwin.dir_entry[0].selected = 1;
	lwin.dir_entry[2].selected = 1;
	lwin.selected_files = 2;

	/* rwin */
	strcpy(rwin.curr_dir, "/rwin");

	rwin.list_rows = 7;
	rwin.list_pos = 5;
	rwin.dir_entry = dynarray_cextend(NULL,
			rwin.list_rows*sizeof(*rwin.dir_entry));
	rwin.dir_entry[0].name = strdup("rfile0");
	rwin.dir_entry[0].origin = &rwin.curr_dir[0];
	rwin.dir_entry[1].name = strdup("rfile1");
	rwin.dir_entry[1].origin = &rwin.curr_dir[0];
	rwin.dir_entry[2].name = strdup("rfile2");
	rwin.dir_entry[2].origin = &rwin.curr_dir[0];
	rwin.dir_entry[3].name = strdup("rfile3");
	rwin.dir_entry[3].origin = &rwin.curr_dir[0];
	rwin.dir_entry[4].name = strdup("rfile4");
	rwin.dir_entry[4].origin = &rwin.curr_dir[0];
	rwin.dir_entry[5].name = strdup("rfile5");
	rwin.dir_entry[5].origin = &rwin.curr_dir[0];
	rwin.dir_entry[6].name = strdup("rdir6");
	rwin.dir_entry[6].origin = &rwin.curr_dir[0];

	rwin.dir_entry[1].selected = 1;
	rwin.dir_entry[3].selected = 1;
	rwin.dir_entry[5].selected = 1;
	rwin.dir_entry[6].selected = 1;
	rwin.selected_files = 4;

	curr_view = &lwin;
	other_view = &rwin;
}

TEARDOWN()
{
	int i;

	update_string(&cfg.shell, NULL);

	for(i = 0; i < lwin.list_rows; i++)
		free(lwin.dir_entry[i].name);
	dynarray_free(lwin.dir_entry);

	for(i = 0; i < rwin.list_rows; i++)
		free(rwin.dir_entry[i].name);
	dynarray_free(rwin.dir_entry);
}

TEST(f)
{
	char *expanded;

	expanded = strdup("");
	expanded = append_selected_files(&lwin, expanded, 0, 0, "", 1);
	assert_string_equal("lfile0 lfile2", expanded);
	free(expanded);

	expanded = strdup("/");
	expanded = append_selected_files(&lwin, expanded, 0, 0, "", 1);
	assert_string_equal("/lfile0 lfile2", expanded);
	free(expanded);

	expanded = strdup("");
	expanded = append_selected_files(&rwin, expanded, 0, 0, "", 1);
	assert_string_equal(SL "rwin" SL "rfile1 " SL "rwin" SL "rfile3 " SL "rwin" SL "rfile5 " SL "rwin" SL "rdir6",
			expanded);
	free(expanded);

	expanded = strdup("/");
	expanded = append_selected_files(&rwin, expanded, 0, 0, "", 1);
	assert_string_equal("/" SL "rwin" SL "rfile1 " SL "rwin" SL "rfile3 " SL "rwin" SL "rfile5 " SL "rwin" SL "rdir6",
			expanded);
	free(expanded);
}

TEST(c)
{
	char *expanded;

	expanded = strdup("");
	expanded = append_selected_files(&lwin, expanded, 1, 0, "", 1);
	assert_string_equal("lfile2", expanded);
	free(expanded);

	expanded = strdup("/");
	expanded = append_selected_files(&lwin, expanded, 1, 0, "", 1);
	assert_string_equal("/lfile2", expanded);
	free(expanded);

	expanded = strdup("");
	expanded = append_selected_files(&rwin, expanded, 1, 0, "", 1);
	assert_string_equal("" SL "rwin" SL "rfile5", expanded);
	free(expanded);

	expanded = strdup("/");
	expanded = append_selected_files(&rwin, expanded, 1, 0, "", 1);
	assert_string_equal("/" SL "rwin" SL "rfile5", expanded);
	free(expanded);
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
