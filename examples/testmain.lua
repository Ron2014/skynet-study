local skynet = require "skynet"
require "skynet.manager"

skynet.register_protocol {
    name = "text",
    id = skynet.PTYPE_TEXT,
    pack = skynet.pack,
    unpack = skynet.tostring,
    dispatch = function(_, address ,msg)
        skynet.log(":%08x(%.2f): %s", address, skynet.time(), msg)
        --print(string.format(":%08x(%.2f): %s", address, skynet.time(), msg))
    end
}

skynet.start(function()
    skynet.error("Server start")

    -- test cservice
    local server = skynet.launch("test", "123")
    local session = skynet.rawsend(server, "text", "456")
    --skynet.log("sendmsg session=%d", session) -- session of skynet.send/rawsend is always 0
    --skynet.send(server, "text", "456")


    -- test luaservice
    server = skynet.newservice("testlua")
    skynet.log("skynet.call result = %s", skynet.call(server, "lua", "add", 1, 2))
    skynet.log("skynet.call result = %s", skynet.call(server, "lua", "add", 2, 5))

    skynet.log("skynet.send result = %s", skynet.send(server, "lua", "print", "hello testlua!"))
    skynet.log("skynet.send result = %s", skynet.send(server, "lua", "print1", "hello testlua!"))
end)
