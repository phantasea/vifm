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

#include "traverser.h"

#include <stddef.h> /* NULL */
#include <stdio.h> /* snprintf() */
#include <stdlib.h> /* free() */

#include "../../compat/os.h"
#include "../../utils/fs.h"
#include "../../utils/path.h"
#include "../../utils/str.h"
#include "../../utils/trie.h"

/* Data used by traverse_subtree(). */
typedef struct
{
	subtree_visitor visitor; /* Callback to invoke for directories and files. */
	void *param;             /* Parameter to pass to the visitor. */
	trie_t *parents;         /* "List" of parents to detect symlink cycles.  NULL
	                            if traversal isn't deep. */
	int deep;                /* Whether symlinks in source path are resolved. */
}
traverse_data_t;

static VisitResult traverse_subtree(const char path[], traverse_data_t *data);
static int add_parent(traverse_data_t *data, const char path[],
		const struct stat *st);
static int remove_parent(traverse_data_t *data, const char path[],
		const struct stat *st);

IoRes
traverse(const char path[], int deep, subtree_visitor visitor, void *param)
{
	/* Duplication with traverse_subtree(), but this way traverse_subtree() can
	 * use information from dirent structure to save some operations. */

	VisitResult visit_result;

	/* Optionally treat symbolic links to directories as files as well. */
	if((!deep && is_symlink(path)) || !is_dir(path))
	{
		visit_result = visitor(path, VA_FILE, deep, param);
	}
	else
	{
		traverse_data_t data = {
			.deep = deep,
			.visitor = visitor,
			.param = param,
		};

		if(deep)
		{
			data.parents = trie_create(/*free_func=*/NULL);
			if(data.parents == NULL)
			{
				return IO_RES_FAILED;
			}
		}

		visit_result = traverse_subtree(path, &data);
		trie_free(data.parents);
	}

	switch(visit_result)
	{
		case VR_OK:        return IO_RES_SUCCEEDED;
		case VR_CANCELLED: return IO_RES_ABORTED;

		default:           return IO_RES_FAILED;
	}
}

/* A generic subtree traversing.  Returns status of visitation. */
static VisitResult
traverse_subtree(const char path[], traverse_data_t *data)
{
	int deep = data->deep;
	subtree_visitor visitor = data->visitor;
	void *param = data->param;

	struct stat dir_st;
	if(deep)
	{
		/* Save the result of stat() both as a tiny optimization and to make sure we
		 * remove the same entry even if path ends up pointing to a different
		 * location at the end of the function. */
		if(os_stat(path, &dir_st) != 0)
		{
			return VR_ERROR;
		}

		if(add_parent(data, path, &dir_st) != 0)
		{
			/* Copy this symbolic link to a directory which makes a cycle as a
			 * file. */
			return visitor(path, VA_FILE, /*deep=*/0, param);
		}
	}

	DIR *dir;
	struct dirent *d;
	VisitResult enter_result;

	dir = os_opendir(path);
	if(dir == NULL)
	{
		return 1;
	}

	enter_result = visitor(path, VA_DIR_ENTER, deep, param);
	if(enter_result == VR_ERROR || enter_result == VR_CANCELLED)
	{
		(void)os_closedir(dir);
		return 1;
	}

	VisitResult result = VR_OK;
	while((d = os_readdir(dir)) != NULL)
	{
		char *full_path;

		if(is_builtin_dir(d->d_name))
		{
			continue;
		}

		full_path = join_paths(path, d->d_name);
		/* Optionally treat symbolic links to directories as files as well. */
		if(deep ? is_dirent_targets_dir(full_path, d) : entry_is_dir(full_path, d))
		{
			result = traverse_subtree(full_path, data);
		}
		else
		{
			result = visitor(full_path, VA_FILE, deep, param);
		}
		free(full_path);

		if(result != VR_OK)
		{
			break;
		}
	}
	(void)os_closedir(dir);

	if(result == VR_OK && enter_result != VR_SKIP_DIR_LEAVE)
	{
		result = visitor(path, VA_DIR_LEAVE, deep, param);
	}

	if(deep && remove_parent(data, path, &dir_st) != 0)
	{
		result = VR_ERROR;
	}

	return result;
}

/* Adds path to the list of active parents.  Returns zero unless hit an
 * insertion error or the parent is already present. */
static int
add_parent(traverse_data_t *data, const char path[], const struct stat *st)
{
	static char mark;

#ifndef _WIN32
	char key[40];
	snprintf(key, sizeof(key), "%llx:%llx", (unsigned long long)st->st_dev,
			(unsigned long long)st->st_ino);
#else
	const char *key = path;
#endif

	void *node_data;
	if(trie_get(data->parents, key, &node_data) == 0 && node_data != NULL)
	{
		return 1;
	}

	/* The value of the data doesn't matter, just setting it to non-NULL. */
	if(trie_set(data->parents, key, &mark) < 0)
	{
		return 1;
	}

	return 0;
}

/* Removes path from the list of active parents.  Returns zero if the parent was
 * in the list and got successfully removed. */
static int
remove_parent(traverse_data_t *data, const char path[], const struct stat *st)
{
#ifndef _WIN32
	char key[40];
	snprintf(key, sizeof(key), "%llx:%llx", (unsigned long long)st->st_dev,
			(unsigned long long)st->st_ino);
#else
	const char *key = path;
#endif

	if(trie_set(data->parents, key, /*data=*/NULL) <= 0)
	{
		return 1;
	}

	return 0;
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
