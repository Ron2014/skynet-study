local skynet = require "skynet"
local sprotoloader = require "sprotoloader"

local max_client = 64

--[[
	main.lua 作为服务器的入口，其作用是【组织服务器的主要业务功能】：
	1. protoloader服务 （.service启动）
	2. console （非守护进程）
	3. debug_console
	4. simpledb
	5. watchdog	监听8888端口，可以和 ./3rd/lua/lua examples/client.lua 配合使用
]]
skynet.start(function()
	skynet.error("Server start")
	skynet.uniqueservice("protoloader")
	if not skynet.getenv "daemon" then
		local console = skynet.newservice("console")
	end
	skynet.newservice("debug_console",8000)			-- 127.0.0.1:8000
	skynet.newservice("simpledb")

	local watchdog = skynet.newservice("watchdog")	-- 0.0.0.0:8888
	skynet.call(watchdog, "lua", "start", {
		port = 8888,
		maxclient = max_client,
		nodelay = true,
	})
	skynet.error("Watchdog listen on", 8888)

	skynet.exit()
end)
