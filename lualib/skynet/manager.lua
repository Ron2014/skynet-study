local skynet = require "skynet"
local c = require "skynet.core"

-- 这是个服务管理器，提供相关接口，向skynet内核发送命令

-- 启动服务，返回句柄
function skynet.launch(...)
	local addr = c.command("LAUNCH", table.concat({...}," "))
	if addr then
		return tonumber("0x" .. string.sub(addr , 2))
	end
end

-- 杀掉服务
-- 如果name是handle，才会让 .launcher 服务清理记录
function skynet.kill(name)
	if type(name) == "number" then
		skynet.send(".launcher","lua","REMOVE",name, true)
		name = skynet.address(name)
	end
	c.command("KILL",name)
end

-- 进程终止
function skynet.abort()
	c.command("ABORT")
end

--[[
	lua服务要起别名时，分两种情况
	1. '.'开头。表示是本地的服务，这个可以注册到全局服务实例存储中（handle_storage 本机环境）去
	2. 没有'.'开头。这种情况是全局的服务，整个服务器集群都可以通过该名称找到唯一的服务，这个名字是 .cslave服务管理的。
		skynet.harbor库 提供了与 .cslave服务通信的接口
--]]
local function globalname(name, handle)
	local c = string.sub(name,1,1)
	assert(c ~= ':')
	if c == '.' then
		return false
	end

	assert(#name <= 16)	-- GLOBALNAME_LENGTH is 16, defined in skynet_harbor.h
	assert(tonumber(name) == nil)	-- global name can't be number

	local harbor = require "skynet.harbor"

	harbor.globalname(name, handle)

	return true
end

-- 给自己这个服务取个别名
function skynet.register(name)
	if not globalname(name) then
		c.command("REG", name)
	end
end

-- 给别的服务取个别名
function skynet.name(name, handle)
	if not globalname(name, handle) then
		c.command("NAME", name .. " " .. skynet.address(handle))
	end
end

local dispatch_message = skynet.dispatch_message

function skynet.forward_type(map, start_func)
	c.callback(function(ptype, msg, sz, ...)
		local prototype = map[ptype]
		if prototype then
			dispatch_message(prototype, msg, sz, ...)
		else
			local ok, err = pcall(dispatch_message, ptype, msg, sz, ...)
			c.trash(msg, sz)
			if not ok then
				error(err)
			end
		end
	end, true)
	skynet.timeout(0, function()
		skynet.init_service(start_func)
	end)
end

function skynet.filter(f ,start_func)
	c.callback(function(...)
		dispatch_message(f(...))
	end)
	skynet.timeout(0, function()
		skynet.init_service(start_func)
	end)
end

function skynet.monitor(service, query)
	local monitor
	if query then
		monitor = skynet.queryservice(true, service)
	else
		monitor = skynet.uniqueservice(true, service)
	end
	assert(monitor, "Monitor launch failed")
	c.command("MONITOR", string.format(":%08x", monitor))
	return monitor
end

return skynet
