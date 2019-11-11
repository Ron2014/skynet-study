local skynet = require "skynet"
local core = require "skynet.core"
require "skynet.manager"	-- import manager apis
local string = string

local services = {}
local command = {}
local instance = {} -- for confirm (function command.LAUNCH / command.ERROR / command.LAUNCHOK)
local launch_session = {} -- for command.QUERY, service_address -> session

local function handle_to_address(handle)
	return tonumber("0x" .. string.sub(handle , 2))
end

local NORET = {}

function command.LIST()
	local list = {}
	for k,v in pairs(services) do
		list[skynet.address(k)] = v
	end
	return list
end

function command.STAT()
	local list = {}
	for k,v in pairs(services) do
		local ok, stat = pcall(skynet.call,k,"debug","STAT")
		if not ok then
			stat = string.format("ERROR (%s)",v)
		end
		list[skynet.address(k)] = stat
	end
	return list
end

function command.KILL(_, handle)
	handle = handle_to_address(handle)
	skynet.kill(handle)
	local ret = { [skynet.address(handle)] = tostring(services[handle]) }
	services[handle] = nil
	return ret
end

function command.MEM()
	local list = {}
	for k,v in pairs(services) do
		local ok, kb = pcall(skynet.call,k,"debug","MEM")
		if not ok then
			list[skynet.address(k)] = string.format("ERROR (%s)",v)
		else
			list[skynet.address(k)] = string.format("%.2f Kb (%s)",kb,v)
		end
	end
	return list
end

function command.GC()
	for k,v in pairs(services) do
		skynet.send(k,"debug","GC")
	end
	return command.MEM()
end

-- 清除记录的服务
function command.REMOVE(_, handle, kill)
	services[handle] = nil
	local response = instance[handle]
	if response then
		-- instance is dead
		response(not kill)	-- return nil to caller of newservice, when kill == false
		instance[handle] = nil
		launch_session[handle] = nil
	end

	-- don't return (skynet.ret) because the handle may exit
	return NORET
end

-- launcher 将自己启动的lua服务（活动的服务）统一管理
local function launch_service(service, ...)
	local param = table.concat({...}, " ")
	local inst = skynet.launch(service, param)

	-- launch_service是在消息处理函数中调用的，running_thread就是它自己，所以session就代表该消息的session
	local session = skynet.context()
	local response = skynet.response()		-- 响应闭包函数
	if inst then
		services[inst] = service .. " " .. param
		instance[inst] = response
		launch_session[inst] = session
	else
		response(false)
		return
	end
	return inst
end

function command.LAUNCH(_, service, ...)
	launch_service(service, ...)
	return NORET
end

function command.LOGLAUNCH(_, service, ...)
	local inst = launch_service(service, ...)
	if inst then
		core.command("LOGON", skynet.address(inst))
	end
	return NORET
end

function command.ERROR(address)
	-- see serivce-src/service_lua.c
	-- init failed
	local response = instance[address]
	if response then
		response(false)
		launch_session[address] = nil
		instance[address] = nil
	end
	services[address] = nil
	return NORET
end

function command.LAUNCHOK(address)
	-- init notice
	local response = instance[address]
	if response then
		response(true, address)
		instance[address] = nil
		launch_session[address] = nil
	end

	return NORET
end

function command.QUERY(_, request_session)
	for address, session in pairs(launch_session) do
		if session == request_session then
			return address
		end
	end
end

-- for historical reasons, launcher support text command (for C service)
-- 历史原因，注册text协议
skynet.register_protocol {
	name = "text",
	id = skynet.PTYPE_TEXT,
	unpack = skynet.tostring,
	dispatch = function(session, address , cmd)
		if cmd == "" then
			command.LAUNCHOK(address)
		elseif cmd == "ERROR" then
			command.ERROR(address)
		else
			error ("Invalid text command " .. cmd)
		end
	end,
}

-- 给lua协议注册dispatch函数
skynet.dispatch("lua", function(session, address, cmd , ...)
	cmd = string.upper(cmd)
	local f = command[cmd]
	if f then
		local ret = f(address, ...)
		if ret ~= NORET then
			-- 我只是一个消息处理函数，为什么执行对应的command后，会得到一个返回值呢？
			-- 很明显这个返回值是要发送给消息源服务的，即 RESPONSE 操作 skynet.ret
			skynet.ret(skynet.pack(ret))
		end
	else
		skynet.ret(skynet.pack {"Unknown command"} )
	end
end)

-- 空的init函数
-- 主要还是看看 skynet.start 中如何接管消息处理
skynet.start(function() end)

--[[
	launcher 是在 bootstrap.lua 中执行以下代码创建的

	local launcher = assert(skynet.launch("snlua","launcher"))
	skynet.name(".launcher", launcher)

	其本质上还是个snlua服务，在环境中形成了3级结构：
	[launcher.lua]	-- 服务逻辑
    [snlua]			-- lua虚拟机
	[skynet]		-- 全局服务实例管理器 handle_storage 统一管理所有服务实例与别名关系

	脚本层使用到的api，分为C和lua
	C-API：./lualib-src/lua-skynet.c
	lua-API：./lualib/skynet.lua ./lualib/skynet/manager.lua

	lua服务的启动逻辑：
	1. 发送消息：skynet.newservice -> skynet.call 使用lua协议向.launcher服务发消息
	2. 处理消息：snlua服务实例的callback -> skynet.dispatch_message
	-> launcher.lua通过skynet.dispatch注册的匿名函数(查找command) -> command.LAUNCH
	-> launch_service -> skynet.launch（你看lancuher都是这样启动的）

	由此可知，每个lua服务都运行在snlua服务实例中的虚拟机中（并没有共享lua虚拟机）
]]
