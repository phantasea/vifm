local M = {
    show_unchanged = true,
}

-- a user may be making changes in a repository quite frequently
local GIT_DIR_TTL = 5
-- repositories are created infrequently
local NON_GIT_DIR_TTL = 60

-- root node of the cache, initialized by M.reset()
local cache

local function find_node(path)
    local current = cache

    for entry in string.gmatch(path, '[^/]+') do
        local next = current.subs[entry]
        if next == nil then
            return nil
        end

        current = next
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
        -- a file removed from index appears twice: first as  'D ' then as '??',
        -- keep the first status
        if node.items[path] == nil then
            node.items[path] = status
            update_dir_status(node, status)
        end
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

function redraw()
    vifm.opts.global.laststatus = not vifm.opts.global.laststatus
    vifm.opts.global.laststatus = not vifm.opts.global.laststatus
end

function update_subdir(sub_at, path, node)
    vifm.startjob {
        cmd = string.format('git -C %s status -z .', vifm.escape(sub_at)),
        onexit = function(sub_job)
            local sub_status_all = sub_job:stdout():read('a')
            local status = sub_status_all == '' and 'GG' or sub_status_all:sub(1, 2)
            set_file_status(node, path, status, node.expires)
            redraw()
        end
    }
end

--- Check the status of git repository subdirectories
function update_subdirs(at, node)
    local cmd = string.format('find %s -mindepth 2 -maxdepth 2 \\( -type d -o -type f \\) -name .git -print0',
                              vifm.escape(at))
    vifm.startjob {
        cmd = cmd,
        onexit = function(job)
            local status_all = job:stdout():read('a')
            node.has_repos = status_all ~= ''
            for entry in string.gmatch(status_all, '[^\0]+') do
                local sub_at = vifm.fnamemodify(entry, ':h')
                local sub_root = vifm.fnamemodify(entry, ':h:t')
                update_subdir(sub_at, sub_root, node)
            end
        end
    }
end

function is_dir(path)
    local f = io.open(path .. "/")
    if f then
        f:close()
        return true
    end
    return false
end

local function shallow(tbl)
    if tbl == nil then
        return nil
    end

    local copy = { }
    for k, v in pairs(tbl) do
        copy[k] = v
    end
    return copy
end

local function fill_node(info)
    local node = info.node
    node.pending = (node.pending or 0) + 1

    vifm.startjob {
        cmd = info.cmd,
        onexit = function(job)
            local output = job:stdout():read('a')
            info.callback(output)

            node.pending = node.pending - 1
            if node.pending == 0 then
                -- Can drop the cache now.
                node.past = nil
                redraw()
            end
        end
    }
end

function M.get(at)
    at = vifm.fnamemodify(at, ':p')
    if vifm.fnamemodify(at, ':t') == '.' then
        at = vifm.fnamemodify(at, ':h')
    end

    local cached = find_node(at)
    if cached ~= nil then
        if cached.expires >= os.time() then
            -- Return old cached data while new one is being retrieved to avoid
            -- flickering.
            -- XXX: this doesn't apply to nested paths.
            return cached.past or cached
        end
    end

    local ts = os.time()
    local root, exit_code = exec(string.format('git -C %s rev-parse --show-toplevel', vifm.escape(at)))
    if exit_code ~= 0 then
        -- Handle directories outside of git repositories and avoid clearing
        -- accumulated cache of its children.
        local node = make_node(cache, at)
        node.expires = ts + NON_GIT_DIR_TTL
        node.in_git = false
        update_subdirs(at, node)
        return node
    end

    -- Make a copy before invoking init_node() on the same node.
    cached = shallow(cached)

    local expires = ts + GIT_DIR_TTL
    local node = init_node(make_node(cache, at), expires)
    node.in_git = true

    node.past = cached
    fill_node {
        node = node,
        cmd = string.format('git -C %s status -z .', vifm.escape(at)),
        callback = function(status_all)
            for entry in string.gmatch(status_all, '[^\0]+') do
                local status = entry:sub(1, 2)
                local abs_path = root..'/'..entry:sub(4)
                local rel_path = abs_path:sub(1 + #at + 1)
                if is_dir(abs_path..'/.git') then
                    -- Strip the end of `rel_path`, to say the contents of the
                    -- sub-repo isn't cached.
                    update_subdir(abs_path, rel_path:sub(1, -2), node)
                else
                    set_file_status(node, rel_path, status, expires)
                end
            end
        end
    }

    if M.show_unchanged then
        fill_node {
            node = node,
            cmd = string.format('git -C %s ls-tree HEAD -r --name-only -z .', vifm.escape(at)),
            callback = function(status_all)
                for rel_path in string.gmatch(status_all, '[^\0]+') do
                    set_file_status(node, rel_path, 'GG', expires)
                end
            end
        }
    end

    return node.past or node
end

function M.reset()
    cache = {
        in_git = false,    -- whether current path is covered by Git
        has_repos = false, -- whether current path is covered by Git
        status = nil,      -- status derived from nested files
        subs = { },        -- subdirectory name -> cache node
        items = { },       -- file name         -> status
        expires = 0        -- when the cache entry expires
    }

    redraw()
end

M.reset()

return M
