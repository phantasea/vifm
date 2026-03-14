#include <stic.h>

#include <stddef.h> /* NULL */

#include <test-utils.h>

#include "../../src/compat/fs_limits.h"
#include "../../src/io/private/ioeta.h"
#include "../../src/io/ioeta.h"
#include "../../src/io/iop.h"

static const io_cancellation_t no_cancellation;

TEST(non_existent_path_yields_zero_size)
{
	ioeta_estim_t *const estim = ioeta_alloc(NULL, no_cancellation);

	ioeta_calculate(estim, "does-not-exist:;", /*shallow=*/0, /*deep=*/0);

	assert_int_equal(1, estim->total_items);
	assert_int_equal(0, estim->current_item);
	assert_int_equal(0, estim->total_bytes);
	assert_int_equal(0, estim->current_byte);

	ioeta_free(estim);
}

TEST(empty_files_are_ok)
{
	ioeta_estim_t *const estim = ioeta_alloc(NULL, no_cancellation);

	ioeta_calculate(estim, TEST_DATA_PATH "/existing-files", /*shallow=*/0,
			/*deep=*/0);

	assert_int_equal(4, estim->total_items);
	assert_int_equal(0, estim->current_item);
	assert_int_equal(0, estim->total_bytes);
	assert_int_equal(0, estim->current_byte);

	ioeta_free(estim);
}

TEST(non_empty_files_are_ok)
{
	ioeta_estim_t *const estim = ioeta_alloc(NULL, no_cancellation);

	ioeta_calculate(estim, TEST_DATA_PATH "/various-sizes", /*shallow=*/0,
			/*deep=*/0);

	assert_int_equal(8, estim->total_items);
	assert_int_equal(0, estim->current_item);
	assert_int_equal(73728, estim->total_bytes);
	assert_int_equal(0, estim->current_byte);

	ioeta_free(estim);
}

TEST(shallow_estimation_does_not_recur)
{
	ioeta_estim_t *const estim = ioeta_alloc(NULL, no_cancellation);

	ioeta_calculate(estim, TEST_DATA_PATH "/various-sizes", /*shallow=*/1,
			/*deep=*/0);

	assert_int_equal(1, estim->total_items);
	assert_int_equal(0, estim->current_item);
	assert_int_equal(0, estim->total_bytes);
	assert_int_equal(0, estim->current_byte);

	ioeta_free(estim);
}

TEST(symlink_calculated_as_zero_bytes, IF(not_windows))
{
	ioeta_estim_t *const estim = ioeta_alloc(NULL, no_cancellation);

	{
		io_args_t args = {
			.arg1.path = TEST_DATA_PATH "/existing-files",
			.arg2.target = SANDBOX_PATH "/link",
		};
		assert_int_equal(IO_RES_SUCCEEDED, iop_ln(&args));
	}

	ioeta_calculate(estim, SANDBOX_PATH "/link", /*shallow=*/0, /*deep=*/0);

	assert_int_equal(1, estim->total_items);
	assert_int_equal(0, estim->current_item);
	assert_int_equal(0, estim->total_bytes);
	assert_int_equal(0, estim->current_byte);

	{
		io_args_t args = {
			.arg1.path = SANDBOX_PATH "/link",
		};
		assert_int_equal(IO_RES_SUCCEEDED, iop_rmfile(&args));
	}

	ioeta_free(estim);
}

TEST(symlink_to_dirs_can_be_followed, IF(not_windows))
{
	ioeta_estim_t *const estim = ioeta_alloc(NULL, no_cancellation);

	{
		char path[PATH_MAX + 1];
		io_args_t args = {
			.arg1.path = path,
			.arg2.target = SANDBOX_PATH "/link",
		};
		make_abs_path(path, sizeof(path), TEST_DATA_PATH, "color-schemes", NULL);
		assert_int_equal(IO_RES_SUCCEEDED, iop_ln(&args));
	}

	ioeta_calculate(estim, SANDBOX_PATH "/link", /*shallow=*/0, /*deep=*/1);

	assert_int_equal(4, estim->total_items);
	assert_int_equal(0, estim->current_item);
	assert_int_equal(107, estim->total_bytes);
	assert_int_equal(0, estim->current_byte);

	{
		io_args_t args = {
			.arg1.path = SANDBOX_PATH "/link",
		};
		assert_int_equal(IO_RES_SUCCEEDED, iop_rmfile(&args));
	}

	ioeta_free(estim);
}

TEST(symlink_to_files_can_be_followed, IF(not_windows))
{
	ioeta_estim_t *const estim = ioeta_alloc(NULL, no_cancellation);

	{
		char path[PATH_MAX + 1];
		io_args_t args = {
			.arg1.path = path,
			.arg2.target = SANDBOX_PATH "/link",
		};
		make_abs_path(path, sizeof(path), TEST_DATA_PATH, "color-schemes/good.vifm",
				NULL);
		assert_int_equal(IO_RES_SUCCEEDED, iop_ln(&args));
	}

	ioeta_calculate(estim, SANDBOX_PATH "/link", /*shallow=*/0, /*deep=*/1);

	assert_int_equal(1, estim->total_items);
	assert_int_equal(0, estim->current_item);
	assert_int_equal(57, estim->total_bytes);
	assert_int_equal(0, estim->current_byte);

	{
		io_args_t args = {
			.arg1.path = SANDBOX_PATH "/link",
		};
		assert_int_equal(IO_RES_SUCCEEDED, iop_rmfile(&args));
	}

	ioeta_free(estim);
}

TEST(dir_link_loop_is_avoided, IF(not_windows))
{
	ioeta_estim_t *const estim = ioeta_alloc(NULL, no_cancellation);

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

	ioeta_calculate(estim, SANDBOX_PATH "/dir", /*shallow=*/0, /*deep=*/1);

	assert_int_equal(2, estim->total_items);
	assert_int_equal(0, estim->current_item);
	assert_int_equal(0, estim->total_bytes);
	assert_int_equal(0, estim->current_byte);

	{
		io_args_t args = {
			.arg1.path = SANDBOX_PATH "/dir/parent",
		};
		assert_int_equal(IO_RES_SUCCEEDED, iop_rmfile(&args));
	}

	{
		io_args_t args = {
			.arg1.path = SANDBOX_PATH "/dir",
		};
		assert_int_equal(IO_RES_SUCCEEDED, iop_rmdir(&args));
	}

	ioeta_free(estim);
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
