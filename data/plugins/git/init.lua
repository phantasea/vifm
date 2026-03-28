local statuses = vifm.plugin.require('statuses')

local M = {}

local function clone(info)
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

local function status_column(info)
    local e = info.entry

    local node = statuses.get(e.location)
    if not node.in_git then
        return { text = '' }
    end

    local status = node.items[e.name]
    if status ~= nil then
        return { text = status }
    end

    local sub = node.subs[e.name]
    if sub ~= nil and sub.status ~= nil then
        return { text = sub.status }
    end

    return { text = '' }
end

-- this does NOT overwrite pre-existing user command
local added = vifm.cmds.add {
    name = "Gclone",
    description = "clone a repository and enter it",
    handler = clone,
    minargs = 1,
    maxargs = 2,
}
if not added then
    vifm.sb.error("Failed to register :Gclone")
end

local added = vifm.addcolumntype {
    name = "GitStatus",
    handler = status_column
}
if not added then
    vifm.sb.error("Failed to add view column GitStatus")
end

return M
