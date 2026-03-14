#include <stic.h>

#include <sys/stat.h> /* chmod() */

#include <test-utils.h>

#include "../../src/compat/fs_limits.h"
#include "../../src/io/iop.h"
#include "../../src/io/ior.h"
#include "../../src/utils/fs.h"

#include "utils.h"

/* Creating symbolic links on Windows requires administrator rights. */
TEST(symlink_to_file_is_symlink_after_copy, IF(not_windows))
{
	{
		io_args_t args = {
			.arg1.path = TEST_DATA_PATH "/read/two-lines",
			.arg2.target = SANDBOX_PATH "/sym-link",
		};
		ioe_errlst_init(&args.result.errors);

		assert_int_equal(IO_RES_SUCCEEDED, iop_ln(&args));
		assert_int_equal(0, args.result.errors.error_count);
	}

	assert_true(is_symlink(SANDBOX_PATH "/sym-link"));

	{
		io_args_t args = {
			.arg1.src = SANDBOX_PATH "/sym-link",
			.arg2.dst = SANDBOX_PATH "/sym-link-copy",
		};
		ioe_errlst_init(&args.result.errors);

		assert_int_equal(IO_RES_SUCCEEDED, ior_cp(&args));
		assert_int_equal(0, args.result.errors.error_count);
	}

	assert_true(is_symlink(SANDBOX_PATH "/sym-link"));
	assert_true(is_symlink(SANDBOX_PATH "/sym-link-copy"));

	{
		io_args_t args = {
			.arg1.path = SANDBOX_PATH "/sym-link",
		};
		ioe_errlst_init(&args.result.errors);

		assert_int_equal(IO_RES_SUCCEEDED, iop_rmfile(&args));
		assert_int_equal(0, args.result.errors.error_count);
	}

	{
		io_args_t args = {
			.arg1.path = SANDBOX_PATH "/sym-link-copy",
		};
		ioe_errlst_init(&args.result.errors);

		assert_int_equal(IO_RES_SUCCEEDED, iop_rmfile(&args));
		assert_int_equal(0, args.result.errors.error_count);
	}
}

/* Creating symbolic links on Windows requires administrator rights. */
TEST(symlink_to_dir_is_symlink_after_copy, IF(not_windows))
{
	{
		io_args_t args = {
			.arg1.path = TEST_DATA_PATH "/read",
			.arg2.target = SANDBOX_PATH "/sym-link",
		};
		ioe_errlst_init(&args.result.errors);

		assert_int_equal(IO_RES_SUCCEEDED, iop_ln(&args));
		assert_int_equal(0, args.result.errors.error_count);
	}

	assert_true(is_symlink(SANDBOX_PATH "/sym-link"));

	{
		io_args_t args = {
			.arg1.src = SANDBOX_PATH "/sym-link",
			.arg2.dst = SANDBOX_PATH "/sym-link-copy",
		};
		ioe_errlst_init(&args.result.errors);

		assert_int_equal(IO_RES_SUCCEEDED, ior_cp(&args));
		assert_int_equal(0, args.result.errors.error_count);
	}

	assert_true(is_symlink(SANDBOX_PATH "/sym-link"));
	assert_true(is_symlink(SANDBOX_PATH "/sym-link-copy"));

	{
		io_args_t args = {
			.arg1.path = SANDBOX_PATH "/sym-link",
		};
		ioe_errlst_init(&args.result.errors);

		assert_int_equal(IO_RES_SUCCEEDED, iop_rmfile(&args));
		assert_int_equal(0, args.result.errors.error_count);
	}

	{
		io_args_t args = {
			.arg1.path = SANDBOX_PATH "/sym-link-copy",
		};
		ioe_errlst_init(&args.result.errors);

		assert_int_equal(IO_RES_SUCCEEDED, iop_rmfile(&args));
		assert_int_equal(0, args.result.errors.error_count);
	}
}

/* Creating symbolic links on Windows requires administrator rights. */
TEST(symlink_to_dir_is_dir_after_copy, IF(not_windows))
{
	{
		char path[PATH_MAX + 1];
		io_args_t args = {
			.arg1.path = path,
			.arg2.target = SANDBOX_PATH "/sym-link",
		};
		ioe_errlst_init(&args.result.errors);

		make_abs_path(path, sizeof(path), TEST_DATA_PATH, "read", NULL);
		assert_int_equal(IO_RES_SUCCEEDED, iop_ln(&args));
		assert_int_equal(0, args.result.errors.error_count);
	}

	assert_true(is_symlink(SANDBOX_PATH "/sym-link"));

	{
		io_args_t args = {
			.arg1.src = SANDBOX_PATH "/sym-link",
			.arg2.dst = SANDBOX_PATH "/sym-link-copy",
			.arg4.deep_copying = 1,
		};
		ioe_errlst_init(&args.result.errors);

		assert_int_equal(IO_RES_SUCCEEDED, ior_cp(&args));
		assert_int_equal(0, args.result.errors.error_count);
	}

	assert_true(is_symlink(SANDBOX_PATH "/sym-link"));
	assert_false(is_symlink(SANDBOX_PATH "/sym-link-copy"));
	assert_true(is_dir(SANDBOX_PATH "/sym-link-copy"));

	{
		io_args_t args = {
			.arg1.path = SANDBOX_PATH "/sym-link",
		};
		ioe_errlst_init(&args.result.errors);

		assert_int_equal(IO_RES_SUCCEEDED, iop_rmfile(&args));
		assert_int_equal(0, args.result.errors.error_count);
	}

	/* Needed to be able to remove files from it. */
	assert_success(chmod(SANDBOX_PATH "/sym-link-copy", 0700));

	{
		io_args_t args = {
			.arg1.path = SANDBOX_PATH "/sym-link-copy",
		};
		ioe_errlst_init(&args.result.errors);

		assert_int_equal(IO_RES_SUCCEEDED, ior_rm(&args));
		assert_int_equal(0, args.result.errors.error_count);
	}
}

/* Creating symbolic links on Windows requires administrator rights. */
TEST(nested_symlink_to_dir_is_dir_after_copy, IF(not_windows))
{
	{
		io_args_t args = {
			.arg1.path = SANDBOX_PATH "/dir",
			.arg3.mode = 0700,
		};
		ioe_errlst_init(&args.result.errors);

		assert_int_equal(IO_RES_SUCCEEDED, iop_mkdir(&args));
		assert_int_equal(0, args.result.errors.error_count);
	}

	{
		char path[PATH_MAX + 1];
		io_args_t args = {
			.arg1.path = path,
			.arg2.target = SANDBOX_PATH "/dir/sym-link",
		};
		ioe_errlst_init(&args.result.errors);

		make_abs_path(path, sizeof(path), TEST_DATA_PATH, "read", NULL);
		assert_int_equal(IO_RES_SUCCEEDED, iop_ln(&args));
		assert_int_equal(0, args.result.errors.error_count);
	}

	assert_true(is_symlink(SANDBOX_PATH "/dir/sym-link"));

	{
		io_args_t args = {
			.arg1.src = SANDBOX_PATH "/dir",
			.arg2.dst = SANDBOX_PATH "/dir-copy",
			.arg4.deep_copying = 1,
		};
		ioe_errlst_init(&args.result.errors);

		assert_int_equal(IO_RES_SUCCEEDED, ior_cp(&args));
		assert_int_equal(0, args.result.errors.error_count);
	}

	assert_true(is_symlink(SANDBOX_PATH "/dir/sym-link"));
	assert_false(is_symlink(SANDBOX_PATH "/dir-copy/sym-link"));
	assert_true(is_dir(SANDBOX_PATH "/dir-copy/sym-link"));

	{
		io_args_t args = {
			.arg1.path = SANDBOX_PATH "/dir",
		};
		ioe_errlst_init(&args.result.errors);

		assert_int_equal(IO_RES_SUCCEEDED, ior_rm(&args));
		assert_int_equal(0, args.result.errors.error_count);
	}

	/* Needed to be able to remove files from it. */
	assert_success(chmod(SANDBOX_PATH "/dir-copy/sym-link", 0700));

	{
		io_args_t args = {
			.arg1.path = SANDBOX_PATH "/dir-copy",
		};
		ioe_errlst_init(&args.result.errors);

		assert_int_equal(IO_RES_SUCCEEDED, ior_rm(&args));
		assert_int_equal(0, args.result.errors.error_count);
	}
}

TEST(dir_link_loop_is_avoided_on_deep_copy, IF(not_windows))
{
	{
		io_args_t args = {
			.arg1.path = SANDBOX_PATH "/dir",
			.arg3.mode = 0700,
		};
		ioe_errlst_init(&args.result.errors);

		assert_int_equal(IO_RES_SUCCEEDED, iop_mkdir(&args));
		assert_int_equal(0, args.result.errors.error_count);
	}
	{
		io_args_t args = {
			.arg1.path = ".",
			.arg2.target = SANDBOX_PATH "/dir/parent",
		};
		assert_int_equal(IO_RES_SUCCEEDED, iop_ln(&args));
	}

	{
		io_args_t args = {
			.arg1.src = SANDBOX_PATH "/dir",
			.arg2.dst = SANDBOX_PATH "/dir-copy",
			.arg4.deep_copying = 1,
		};
		ioe_errlst_init(&args.result.errors);

		assert_int_equal(IO_RES_SUCCEEDED, ior_cp(&args));
		assert_int_equal(0, args.result.errors.error_count);
	}

	{
		io_args_t args = {
			.arg1.path = SANDBOX_PATH "/dir",
		};
		ioe_errlst_init(&args.result.errors);

		assert_int_equal(IO_RES_SUCCEEDED, ior_rm(&args));
		assert_int_equal(0, args.result.errors.error_count);
	}
	{
		io_args_t args = {
			.arg1.path = SANDBOX_PATH "/dir-copy/parent",
		};
		assert_int_equal(IO_RES_SUCCEEDED, iop_rmfile(&args));
	}
	{
		io_args_t args = {
			.arg1.path = SANDBOX_PATH "/dir-copy",
		};
		assert_int_equal(IO_RES_SUCCEEDED, iop_rmdir(&args));
	}
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
