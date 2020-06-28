local skynet = require "skynet"
local gateserver = require "snax.gateserver"

local watchdog
local connection = {}	-- fd -> connection : { fd , client, agent , ip, mode }
local forwarding = {}	-- agent -> connection

-- 客户端消息协议
skynet.register_protocol {
	name = "client",
	id = skynet.PTYPE_CLIENT,
}

--[[
	下面是两份配置，handle 和 CMD。CMD 相当于 handler.command 的命令手册。
	gate 服务的启动由 gateserver.start 完成。

	gate 的消息应该分两类：
	1. 从其他服务发来的消息。协议为lua
	2. 从客户端发来的消息。协议为socket

	handle = {
		open			-> 记下 watchdog
		message

		connect											= 通知watchdog操作agent
		disconnect		-> close_fd -> unforward		= 通知watchdog操作agent
		error			-> close_fd -> unforward		= 通知watchdog操作agent
		warning											= 通知watchdog操作agent
		
		command
	}

	CMD = {
		forward
		accept
		kick
	}
]]

local handler = {}

function handler.open(source, conf)
	watchdog = conf.watchdog or source
end

function handler.message(fd, msg, sz)
	-- recv a package, forward it
	local c = connection[fd]
	local agent = c.agent
	if agent then
		-- It's safe to redirect msg directly , gateserver framework will not free msg.
		skynet.redirect(agent, c.client, "client", fd, msg, sz)
	else
		skynet.send(watchdog, "lua", "socket", "data", fd, skynet.tostring(msg, sz))
		-- skynet.tostring will copy msg to a string, so we must free msg here.
		skynet.trash(msg,sz)
	end
end

function handler.connect(fd, addr)
	local c = {
		fd = fd,
		ip = addr,
	}
	connection[fd] = c
	skynet.send(watchdog, "lua", "socket", "open", fd, addr)
end

local function unforward(c)
	if c.agent then
		forwarding[c.agent] = nil
		c.agent = nil
		c.client = nil
	end
end

local function close_fd(fd)
	local c = connection[fd]
	if c then
		unforward(c)
		connection[fd] = nil
	end
end

function handler.disconnect(fd)
	close_fd(fd)
	skynet.send(watchdog, "lua", "socket", "close", fd)
end

function handler.error(fd, msg)
	close_fd(fd)
	skynet.send(watchdog, "lua", "socket", "error", fd, msg)
end

function handler.warning(fd, size)
	skynet.send(watchdog, "lua", "socket", "warning", fd, size)
end

local CMD = {}

function CMD.forward(source, fd, client, address)
	local c = assert(connection[fd])
	unforward(c)
	c.client = client or 0
	c.agent = address or source
	forwarding[c.agent] = c
	gateserver.openclient(fd)
end

function CMD.accept(source, fd)
	local c = assert(connection[fd])
	unforward(c)
	gateserver.openclient(fd)
end

function CMD.kick(source, fd)
	gateserver.closeclient(fd)
end

function handler.command(cmd, source, ...)
	local f = assert(CMD[cmd])
	return f(source, ...)
end

gateserver.start(handler)
