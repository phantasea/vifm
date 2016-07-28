/* vifm
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

#ifndef VIFM__ENGINE__KEYS_H__
#define VIFM__ENGINE__KEYS_H__

#include <stddef.h> /* size_t wchar_t */

enum
{
	NO_COUNT_GIVEN = -1,
	NO_REG_GIVEN = -1,
};

enum
{
	MF_USES_REGS = 1,
	MF_USES_COUNT = 2,
	MF_USES_INPUT = 4,
};

enum
{
	KEYS_UNKNOWN    = -1024,
	KEYS_WAIT       = -2048,
	KEYS_WAIT_SHORT = -4096,
};

#define IS_KEYS_RET_CODE(c) \
		({ \
			const int tmp = (c); \
			tmp == KEYS_UNKNOWN || tmp == KEYS_WAIT || tmp == KEYS_WAIT_SHORT; \
		})

typedef enum
{
	FOLLOWED_BY_NONE,
	FOLLOWED_BY_SELECTOR,
	FOLLOWED_BY_MULTIKEY,
}
FollowedBy;

/* Describes single key (command or selector) on its own. */
typedef struct
{
	int count; /* Repeat count, may be equal NO_COUNT_GIVEN. */
	int reg;   /* Number of selected register. */
	int multi; /* Multikey. */
}
key_info_t;

/* Describes sequence of keys (it can consist of a single key).  This structure
 * is shared among elements composite constructs like command+selector. */
typedef struct
{
	int selector;   /* Selector passed. */
	int count;      /* Count of selected items. */
	int *indexes;   /* Item indexes. */
	int after_wait; /* After short timeout. */
	int mapped;     /* Not users input. */
	int recursive;  /* The key is from recursive call of execute_keys_*(...). */
}
keys_info_t;

/* Handler for builtin keys. */
typedef void (*vle_keys_handler)(key_info_t key_info, keys_info_t *keys_info);
/* Type of function invoked by vle_keys_list() and vle_keys_suggest().  rhs is
 * provided for user-defined keys and is empty otherwise.  Description is empty
 * for user-defined keys or when not set. */
typedef void (*vle_keys_list_cb)(const wchar_t lhs[], const wchar_t rhs[],
		const char descr[]);
/* User-provided suggestion callback for multikeys. */
typedef void (*vle_suggest_func)(vle_keys_list_cb cb);

/* Type of callback that handles all keys uncaught by shortcuts.  Should return
 * zero on success and non-zero on error. */
typedef int (*default_handler)(wchar_t key);

typedef struct
{
	union
	{
		vle_keys_handler handler; /* Handler for builtin commands. */
		wchar_t *cmd;             /* Mapped value for user-defined keys. */
	}
	data;
	FollowedBy followed;        /* What type of key should we wait for. */
	vle_suggest_func suggest;   /* Suggestion function (can be NULL).  Invoked for
	                               multikeys. */
	const char *descr;          /* Brief description of the key. */
	int nim;                    /* Whether additional count in the middle is
	                               allowed. */
	int skip_suggestion;        /* Do not print this among suggestions. */
}
key_conf_t;

typedef struct
{
	const wchar_t keys[5];
	key_conf_t info;
}
keys_add_info_t;

/* Initializes the unit.  Assumed that key_mode_flags is an array of at least
 * modes_count items. */
void vle_keys_init(int modes_count, int *key_mode_flags);

/* Frees all memory allocated by the unit and returns it to initial state. */
void vle_keys_reset(void);

/* Removes just user-defined keys (leaving builtin keys intact). */
void vle_keys_user_clear(void);

/* Set handler for unregistered keys.  handler can be NULL to remove it. */
void vle_keys_set_def_handler(int mode, default_handler handler);

/* Process sequence of keys.  Return value:
 *  - 0 - success
 *  - KEYS_*
 *  - something else from the default key handler */
int vle_keys_exec(const wchar_t keys[]);

/* Same as vle_keys_exec(), but disallows map processing in RHS of maps. */
int vle_keys_exec_no_remap(const wchar_t keys[]);

/* Same as vle_keys_exec(), but assumes that key wait timeout has expired. */
int vle_keys_exec_timed_out(const wchar_t keys[]);

/* Same as vle_keys_exec_no_remap(), but assumes that key wait timeout has
 * expired. */
int vle_keys_exec_timed_out_no_remap(const wchar_t keys[]);

/* Registers cmds[0 .. len-1] commands for the mode.  Returns non-zero on error,
 * otherwise zero is returned. */
int vle_keys_add(keys_add_info_t cmds[], size_t len, int mode);

/* Registers cmds[0 .. len-1] selectors for the mode.  Returns non-zero on
 * error, otherwise zero is returned. */
int vle_keys_add_selectors(keys_add_info_t cmds[], size_t len, int mode);

/* Registers user key mapping.  Returns non-zero or error, otherwise zero is
 * returned. */
int vle_keys_user_add(const wchar_t keys[], const wchar_t rhs[], int mode,
		int no_r);

/* Checks whether given user mapping exists.  Returns non-zero if so, otherwise
 * zero is returned. */
int vle_keys_user_exists(const wchar_t keys[], int mode);

/* Removes user mapping from the mode.  Returns non-zero if given key sequence
 * wasn't found. */
int vle_keys_user_remove(const wchar_t keys[], int mode);

/* Lists all or just user keys of the given mode with description. */
void vle_keys_list(int mode, vle_keys_list_cb cb, int user_only);

/* Retrieves number of keys processed so far.  Clients are expected to use
 * difference of returned values.  Returns the number. */
size_t vle_keys_counter(void);

/* Checks whether a mapping handler is currently been executed.  Returns
 * non-zero if so, otherwise zero is returned. */
int vle_keys_inside_mapping(void);

/* Invokes cb for each possible keys continuation.  Intended to be used on
 * KEYS_WAIT and KEYS_WAIT_SHORT returns. */
void vle_keys_suggest(const wchar_t keys[], vle_keys_list_cb cb,
		int custom_only);

#endif /* VIFM__ENGINE__KEYS_H__ */

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0: */
/* vim: set cinoptions+=t0 filetype=c : */
