local statuses = vifm.plugin.require('statuses')

local M = {}

-- array of {pattern, replacement}
local map_rules = { }

local function clone_command(info)
    local url = info.argv[1]

    local name
    if #info.argv > 1 then
        name = info.argv[2]
    else
        name = vifm.fnamemodify(url, ":t:s/\\.git$//", "")
    end

    vifm.sb.quick(string.format('Cloning %q to %q...', url, name))

    local job = vifm.startjob {
        cmd = string.format('git clone %q %q', url, name)
    }
    for line in job:stdout():lines() do
        vifm.sb.quick(line)
    end

    if job:exitcode() == 0 then
        vifm.currview():cd(name)
    else
        local errors = job:errors()
        if #errors == 0 then
            vifm.errordialog('Gclone failed to clone',
                             'Error message is not available.')
        else
            vifm.errordialog('Gclone failed to clone', errors)
        end
    end
end

local function map_command(info)
    for _, entry in ipairs(map_rules) do
        if entry[1] == info.argv[1] and entry[2] == info.argv[2] then
            -- don't add duplicates
            return
        end
    end

    map_rules[#map_rules + 1] = info.argv
end

local function char_matches(char, pattern)
    return pattern == '*' or char == pattern
end

local function status_matches(status, pattern)
    return char_matches(status:sub(1, 1), pattern:sub(1, 1))
       and char_matches(status:sub(2, 2), pattern:sub(2, 2))
end

local function map_status(status)
    for _, entry in ipairs(map_rules) do
        if status_matches(status, entry[1]) then
            return entry[2]
        end
    end
    return status
end

local function status_column(info)
    local e = info.entry

    local node = statuses.get(e.location)
    if not node.in_git then
        return { text = '' }
    end

    local status = node.items[e.name]
    if status ~= nil then
        return { text = map_status(status) }
    end

    local sub = node.subs[e.name]
    if sub ~= nil and sub.status ~= nil then
        return { text = map_status(sub.status) }
    end

    return { text = '' }
end

local function add_cmd(info)
    -- this does NOT overwrite pre-existing user command
    local added = vifm.cmds.add(info)
    if not added then
        vifm.sb.error(string.format("Failed to register %s", info.name))
    end
end

add_cmd {
    name = "Gclone",
    description = "clone a repository and enter it",
    handler = clone_command,
    minargs = 1,
    maxargs = 2,
}

add_cmd {
    name = "Gmap",
    description = "map status to a custom value",
    handler = map_command,
    minargs = 2,
}

local added = vifm.addcolumntype {
    name = "GitStatus",
    handler = status_column
}
if not added then
    vifm.sb.error("Failed to add view column GitStatus")
end

return M
