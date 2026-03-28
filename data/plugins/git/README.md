## Git

This plugin provides utilities related to Git.

### `:Gclone` command

Clones repository, optionally deriving target directory name from the URL.  On
success, enters the newly cloned worktree.  On failure, shows the error in a
dialog.

**Examples:**

 * Clone into directory called `vifm`:
   ```
   :Gclone https://github.com/vifm/vifm.git
   ```

 * Clone into directory called `vifm-dev`:
   ```
   :Gclone https://github.com/vifm/vifm.git vifm-dev
   ```

**Parameters:**

 1. URL to clone from (required).
 2. Destination (optional).  Derived from the last component of the URL removing
    `.git` suffix.

### `GitStatus` view column

When inside a Git repository, displays status of files and directories in a way
similar to how `git status --short` does it.

**Possible column values for files:**

| Value | Meaning
| ----- | -------
| `  `  | Ignored or unchanged.
| ` M`  | Modified in worktree.
| `MM`  | Modified in index and in worktree.
| `AM`  | Added in index, modified in worktree.
| `RM`  | Renamed in index, modified in worktree.
| `M `  | Modified in index.
| `A `  | Added in Index.
| `R `  | Renamed in index.
| `D `  | Deleted in index.
| ` D`  | Deleted in worktree.
| `MD`  | Modified in index, deleted in worktree.
| `AD`  | Added in index, deleted in worktree.
| `RD`  | Renamed in index, deleted in worktree.
| `UD`  | Updated here, but deleted in merged changes.
| `DU`  | Deleted here, but updated in merged changes.
| `AA`  | Added here and in merged changes.
| `UU`  | Modified here and in merged changes.
| `??`  | Untracked.

**Possible column values for directories:**

 * Any of the values for files.
 * If a subtree has multiple files modified, each character of the status is
   merged like this:
   * space + space = space
   * space + non-space = non-space
   * non-space + identical non-space = non-space
   * non-space + different non-space = 'X'
   * 'X' + anything = 'X'

**Example:**

 * `:set viewcolumns=-3{GitStatus},*{name}..,6{}.`

**Issues:**

 * Has a noticeable initial delay for large Git repositories due to fetching
   statuses synchronously.

**TODO:**

 * Run `git status -z` asynchronously to not block directory traversal.
 * Make cache update faster if directory modification is detected.
 * Check behaviour with submodules.
 * Check if `!!` status can ever be seen.
 * Consider using `git status --porcelain=v2 -z` as it provides more
   information.
 * Consider marking ignored files as such.
 * Using `libgit2` could improve performance.
