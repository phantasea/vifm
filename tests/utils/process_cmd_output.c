#include <stic.h>

#include <unistd.h> /* chdir() unlink() */

#include <stdio.h> /* fclose() fopen() fprintf() */

#include "../../src/cfg/config.h"
#include "../../src/utils/fs.h"
#include "../../src/utils/str.h"
#include "../../src/utils/utils.h"
#include "../../src/cmd_completion.h"

static void line_handler(const char line[], void *arg);
static int cat_is_available(void);

static int nlines;

TEST(check_null_separation, IF(cat_is_available))
{
	char *saved_cwd;

	FILE *const f = fopen(SANDBOX_PATH "/list", "w");
	fprintf(f, "%s%c", SANDBOX_PATH "/a\nb", '\0');
	fclose(f);

	saved_cwd = save_cwd();

	assert_success(chdir(SANDBOX_PATH));

#ifndef _WIN32
	replace_string(&cfg.shell, "/bin/sh");
#else
	replace_string(&cfg.shell, "cmd");
#endif
	stats_update_shell_type(cfg.shell);

	nlines = 0;
	assert_success(process_cmd_output("tests", "cat list", 1, 0, &line_handler,
				NULL));
	assert_int_equal(1, nlines);

	stats_update_shell_type("/bin/sh");
	update_string(&cfg.shell, NULL);

	assert_success(unlink("list"));

	restore_cwd(saved_cwd);
}

static void
line_handler(const char line[], void *arg)
{
	++nlines;
}

static int
cat_is_available(void)
{
	return external_command_exists("cat");
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
