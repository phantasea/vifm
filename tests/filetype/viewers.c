#include <stic.h>

#include <stdlib.h>
#include <string.h>

#include "../../src/int/file_magic.h"
#include "../../src/filetype.h"
#include "../../src/status.h"
#include "../../src/utils/str.h"
#include "test.h"

static int
prog1_available(const char name[])
{
	return stroscmp(name, "prog1") == 0;
}

static int
prog2_available(const char name[])
{
	return stroscmp(name, "prog2") == 0;
}

static int
nothing_available(const char name[])
{
	return 0;
}

TEST(null_if_nothing_set)
{
	assert_true(ft_get_viewer("file.version.tar.bz2") == NULL);
}

TEST(multiple_choice_separated)
{
	const char *viewer;

	set_viewers("*.tar.bz2", "prog1");
	set_viewers("*.tar.bz2", "prog2");

	ft_init(&prog1_available);
	viewer = ft_get_viewer("file.version.tar.bz2");
	assert_string_equal("prog1", viewer);

	ft_init(&prog2_available);
	viewer = ft_get_viewer("file.version.tar.bz2");
	assert_string_equal("prog2", viewer);

	ft_init(&nothing_available);
	viewer = ft_get_viewer("file.version.tar.bz2");
	assert_true(viewer == NULL);
}

TEST(multiple_choice_joined)
{
	const char *viewer;

	set_viewers("*.tar.bz2", "prog1,prog2");

	ft_init(&prog1_available);
	viewer = ft_get_viewer("file.version.tar.bz2");
	assert_string_equal("prog1", viewer);

	ft_init(&prog2_available);
	viewer = ft_get_viewer("file.version.tar.bz2");
	assert_string_equal("prog2", viewer);

	ft_init(&nothing_available);
	viewer = ft_get_viewer("file.version.tar.bz2");
	assert_true(viewer == NULL);
}

TEST(description_is_not_allowed)
{
	const char *viewer;

	set_viewers("*.tar.bz2", "{archives} prog1");

	ft_init(&prog1_available);
	viewer = ft_get_viewer("file.version.tar.bz2");
	assert_true(viewer == NULL);
}

TEST(several_patterns)
{
	set_viewers("*.tbz,*.tbz2,*.tar.bz2", "prog1");

	ft_init(&prog1_available);

	assert_true(ft_get_viewer("file.version.tbz") != NULL);
	assert_true(ft_get_viewer("file.version.tbz2") != NULL);
	assert_true(ft_get_viewer("file.version.tar.bz2") != NULL);
}

TEST(pattern_list, IF(has_mime_type_detection))
{
	char cmd[1024];

	snprintf(cmd, sizeof(cmd), "<%s>{binary-data}",
			get_mimetype(TEST_DATA_PATH "/read/binary-data"));
	set_viewers(cmd, "prog1");

	ft_init(&prog1_available);

	assert_string_equal("prog1",
			ft_get_viewer(TEST_DATA_PATH "/read/binary-data"));
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
