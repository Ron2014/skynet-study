local skynet = require "skynet"

local CMD = {}

function CMD.add(a, b)
    return a+b
end

function CMD.print(o)
    skynet.error(o)
end

skynet.dispatch("lua", function(session, source, cmd, ...)
    skynet.log("session=%s source=%s cmd=%s params=%s", session, source, cmd, table.concat({...}, " "))
    local f = CMD[cmd]
    assert(f, "can't find cmd " .. (cmd or "nil"))

    if session == 0 then
        f(...)
    else
        skynet.ret(skynet.pack(f(...)))
    end
end)

skynet.start(function() end)
