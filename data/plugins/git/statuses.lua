local M = { }

-- a user may be making changes in a repository quite frequently
local GIT_DIR_TTL = 5
-- repositories are created infrequently
local NON_GIT_DIR_TTL = 60

local cache = {
    in_git = false, -- whether current path is covered by Git
    status = nil,   -- status derived from nested files
    subs = { },     -- subdirectory name -> cache node
    items = { },    -- file name -> status
    expires = 0     -- when the cache entry expires
}

local function find_node(path)
    local current = cache

    for entry in string.gmatch(path, '[^/]+') do
        local next = current.subs[entry]
        if next == nil then
            return nil
        end

        current = next
    end

    if os.time() > current.expires then
        return nil
    end

    return current
end

local function init_node(node, expires)
    node.subs = {}
    node.items = {}
    node.expires = expires
    node.status = nil
    return node
end

local function make_node(node, path)
    local current = node

    for entry in string.gmatch(path, '[^/]+') do
        local next = current.subs[entry]
        if next == nil then
            next = init_node({}, 0)
            current.subs[entry] = next
        end

        current = next
    end

    return current
end

local function combine_status(a, b)
    if a == b then
        return a
    end
    if a == ' ' then
        return b
    end
    if b == ' ' then
        return a
    end
    return 'X'
end

local function update_dir_status(node, status)
    if node.status == nil then
        node.status = status
    else
        local staged = combine_status(node.status:sub(1, 1), status:sub(1, 1))
        local index = combine_status(node.status:sub(2, 2), status:sub(2, 2))
        node.status = staged..index
    end
end

local function set_file_status(node, path, status, expires)
    local slash = path:find('/')
    if slash == nil then
        node.items[path] = status
        update_dir_status(node, status)
        return node.status
    end

    local entry = path:sub(1, slash - 1)
    path = path:sub(slash + 1)

    local next = node.subs[entry]
    if next == nil then
        next = init_node({}, expires)
        next.in_git = true
        node.subs[entry] = next
    end

    status = set_file_status(next, path, status, expires)
    update_dir_status(node, status)

    return node.status
end

local function exec(cmd)
    local job = vifm.startjob { cmd = cmd }
    local result = job:stdout():read('a')
    if result:sub(#result) == '\n' then
        result = result:sub(1, #result - 1)
    end
    return result, job:exitcode()
end

function M.get(at)
    at = vifm.fnamemodify(at, ':p')
    if vifm.fnamemodify(at, ':t') == '.' then
        at = vifm.fnamemodify(at, ':h')
    end

    local cached = find_node(at)
    if cached ~= nil then
        return cached
    end

    local ts = os.time()
    local root, exit_code = exec(string.format('git -C %s rev-parse --show-toplevel', vifm.escape(at)))
    if exit_code ~= 0 then
        -- Handle directories outside of git repositories and avoid clearing
        -- accumulated cache of its children.
        local node = make_node(cache, at)
        node.expires = ts + NON_GIT_DIR_TTL
        node.in_git = false
        return node
    end

    local expires = ts + GIT_DIR_TTL
    local node = init_node(make_node(cache, at), expires)
    node.in_git = true

    local subdirs = exec(string.format('git -C %s ls-tree -r -d --name-only -z HEAD .', vifm.escape(at)))
    for subdir in string.gmatch(subdirs, '[^\0]+') do
        if subdir == '../' then
            node.status = ''
            -- no need to call `git status`
            return node
        end

        if subdir ~= './' then
            local dir = make_node(node, subdir)
            dir.expires = expires
            dir.in_git = true
            dir.status = '  '
        end
    end

    local status = exec(string.format('git -C %s status -z .', vifm.escape(at)))
    for entry in string.gmatch(status, '[^\0]+') do
        local status = entry:sub(1, 2)
        local abs_path = root..'/'..entry:sub(4)
        local rel_path = abs_path:sub(1 + #at + 1)
        set_file_status(node, rel_path, status, expires)
    end

    return node
end

return M
