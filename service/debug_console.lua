local skynet = require "skynet"
local codecache = require "skynet.codecache"
local core = require "skynet.core"
local socket = require "skynet.socket"
local snax = require "skynet.snax"
local memory = require "skynet.memory"
local httpd = require "http.httpd"
local sockethelper = require "http.sockethelper"

local arg = table.pack(...)
assert(arg.n <= 2)
local ip = (arg.n == 2 and arg[1] or "127.0.0.1")
local port = tonumber(arg[arg.n])

local COMMAND = {}		-- 基础命令
local COMMANDX = {}		-- 扩展命令

-- 将散列表转换成一条字符串
local function format_table(t)
	-- 对散列表的 key 进行升序
	local index = {}
	for k in pairs(t) do
		table.insert(index, k)
	end
	table.sort(index, function(a, b) return tostring(a) < tostring(b) end)

	-- 生成 key:value
	-- 最后1个t, 代表一行 entry 信息
	local result = {}
	for _,v in ipairs(index) do
		table.insert(result, string.format("%s:%s",v,tostring(t[v])))
	end
	return table.concat(result,"\t")
end

-- 将一行信息打印, value 即 entry
local function dump_line(print, key, value)
	if type(value) == "table" then
		print(key, format_table(value))
	else
		print(key,tostring(value))
	end
end

-- 将一个散列表的内容一行行打印
local function dump_list(print, list)
	local index = {}
	for k in pairs(list) do
		table.insert(index, k)
	end
	table.sort(index, function(a, b) return tostring(a) < tostring(b) end)

	-- 将散列表转成数组, 按 key 升序
	-- 逐行输出
	for _,v in ipairs(index) do
		dump_line(print, v, list[v])
	end
end

-- 对命令行按空格分割
local function split_cmdline(cmdline)
	local split = {}
	for i in string.gmatch(cmdline, "%S+") do
		table.insert(split,i)
	end
	return split
end

--[[
	服务分两类:
	1. COMMAND		普通:
	2. COMMANDX		扩展: call debug
--]]
local function docmd(cmdline, print, fd)
	local split = split_cmdline(cmdline)
	local command = split[1]
	local cmd = COMMAND[command]
	local ok, list
	if cmd then
		ok, list = pcall(cmd, table.unpack(split,2))
	else
		cmd = COMMANDX[command]
		if cmd then
			split.fd = fd
			split[1] = cmdline	-- 完整命令行
			ok, list = pcall(cmd, split)
		else
			print("Invalid command, type help for command list")
		end
	end

	if ok then
		if list then
			if type(list) == "string" then
				print(list)
			else
				dump_list(print, list)
			end
		end
		print("<CMD OK>")
	else
		print(list)
		print("<CMD Error>")
	end
end

local function console_main_loop(stdin, print)
	print("Welcome to skynet console")
	skynet.error(stdin, "connected")
	local ok, err = pcall(function()
		while true do
			local cmdline = socket.readline(stdin, "\n")
			if not cmdline then
				break
			end
			if cmdline:sub(1,4) == "GET " then
				-- http
				local code, url = httpd.read_request(sockethelper.readfunc(stdin, cmdline.. "\n"), 8192)
				local cmdline = url:sub(2):gsub("/"," ")
				docmd(cmdline, print, stdin)
				break
			end
			if cmdline ~= "" then
				docmd(cmdline, print, stdin)
			end
		end
	end)
	if not ok then
		skynet.error(stdin, err)
	end
	skynet.error(stdin, "disconnected")
	socket.close(stdin)
end

skynet.start(function()
	local listen_socket = socket.listen (ip, port)
	skynet.error("Start debug console at " .. ip .. ":" .. port)
	socket.start(listen_socket , function(id, addr)
		local function print(...)
			local t = { ... }
			for k,v in ipairs(t) do
				t[k] = tostring(v)
			end
			socket.write(id, table.concat(t,"\t"))
			socket.write(id, "\n")
		end
		socket.start(id)
		skynet.fork(console_main_loop, id , print)
	end)
end)

function COMMAND.help()
	return {
		help = "This help message",
		list = "List all the service",
		stat = "Dump all stats",
		info = "info address : get service infomation",
		exit = "exit address : kill a lua service",
		kill = "kill address : kill service",
		mem = "mem : show memory status",
		gc = "gc : force every lua service do garbage collect",
		start = "lanuch a new lua service",
		snax = "lanuch a new snax service",
		clearcache = "clear lua code cache",
		service = "List unique service",
		task = "task address : show service task detail",
		uniqtask = "task address : show service unique task detail",
		inject = "inject address luascript.lua",
		logon = "logon address",
		logoff = "logoff address",
		log = "launch a new lua service with log",
		debug = "debug address : debug a lua service",
		signal = "signal address sig",
		cmem = "Show C memory info",
		ping = "ping address",
		call = "call address ...",
		trace = "trace address [proto] [on|off]",
		netstat = "netstat : show netstat",
		profactive = "profactive [on|off] : active/deactive jemalloc heap profilling",
		dumpheap = "dumpheap : dump heap profilling",
	}
end

function COMMAND.clearcache()
	codecache.clear()
end

--[[
	启动服务的几个方法
1. COMMAND.start

skynet.newservice
本质是通知 .launcher 服务通过 LAUNCH 方式 启动 snlua

2. COMMAND.log

skynet.call, ".launcher", "lua", "LOGLAUNCH", "snlua", ...
.launcher 服务的 LOGLAUNCH 方式, 也就多个 log 服务实例句柄的操作

3. COMMAND.snax

snax.newservice
]]


-- start service_name 用 skynet.newservice 启动一个新的 lua 服务。
function COMMAND.start(...)
	local ok, addr = pcall(skynet.newservice, ...)
	if ok then
		if addr then
			return { [skynet.address(addr)] = ... }
		else
			return "Exit"
		end
	else
		return "Failed"
	end
end

function COMMAND.log(...)
	local ok, addr = pcall(skynet.call, ".launcher", "lua", "LOGLAUNCH", "snlua", ...)
	if ok then
		if addr then
			return { [skynet.address(addr)] = ... }
		else
			return "Failed"
		end
	else
		return "Failed"
	end
end

-- snax service_name 用 snax.newservice 启动一个新的 snax 服务。
function COMMAND.snax(...)
	local ok, s = pcall(snax.newservice, ...)
	if ok then
		local addr = s.handle
		return { [skynet.address(addr)] = ... }
	else
		return "Failed"
	end
end

-- standalone 模式下会启动 SERVICE 单点服务
-- 使用 SERVICE 服务列出所有唯一 lua 服务
-- 并显示出请求还不存在的服务被挂起的请求。
function COMMAND.service()
	return skynet.call("SERVICE", "lua", "LIST")
end

local function adjust_address(address)
	local prefix = address:sub(1,1)
	if prefix == '.' then		-- 单点服务的名称
		return assert(skynet.localname(address), "Not a valid name")
	elseif prefix ~= ':' then	-- 单点服务的句柄
		address = assert(tonumber("0x" .. address), "Need an address") | (skynet.harbor(skynet.self()) << 24)
	end
	return address
end

--[[
	因为所有服务都会通过 launcher 启动
]]

-- list 列出所有服务，以及启动服务的命令参数。
function COMMAND.list()
	return skynet.call(".launcher", "lua", "LIST")
end

-- stat 列出所有 lua 服务的消息队列长度，以及被挂起的请求数量，处理的消息总数。
-- 如果在 Config 里设置 profile 为 true(默认skynet_main.c)，还会报告服务使用的 cpu 时间。
function COMMAND.stat()
	return skynet.call(".launcher", "lua", "STAT")
end

-- mem 让所有 lua 服务汇报自己占用的内存(通过垃圾收集器)。
function COMMAND.mem()
	return skynet.call(".launcher", "lua", "MEM")
end

-- kill address 强制中止一个 lua 服务。
-- launcher:KILL => launcher:REMOVE
function COMMAND.kill(address)
	return skynet.call(".launcher", "lua", "KILL", address)
end

-- gc 强制让所有 lua 服务都执行一次垃圾回收，并报告回收后的内存。
function COMMAND.gc()
	return skynet.call(".launcher", "lua", "GC")
end

-- exit address 让一个 lua 服务自行退出。
function COMMAND.exit(address)
	skynet.send(adjust_address(address), "debug", "EXIT")
end

-- inject address script 将 script 名字对应的脚本插入到指定服务中运行（通常可用于热更新补丁）。
function COMMAND.inject(address, filename, ...)
	address = adjust_address(address)
	local f = io.open(filename, "rb")
	if not f then
		return "Can't open " .. filename
	end
	local source = f:read "*a"
	f:close()
	local ok, output = skynet.call(address, "debug", "RUN", source, filename, ...)
	if ok == false then
		error(output)
	end
	return output
end

-- task address 显示一个服务中所有被挂起的请求的调用栈。
function COMMAND.task(address)
	address = adjust_address(address)
	return skynet.call(address,"debug","TASK")
end

function COMMAND.uniqtask(address)
	address = adjust_address(address)
	return skynet.call(address,"debug","UNIQTASK")
end

-- 显示服务的自定义信息
function COMMAND.info(address, ...)
	address = adjust_address(address)
	return skynet.call(address,"debug","INFO", ...)
end

-- debug address 针对一个 lua 服务启动内置的单步调试器。
-- 进入交互模式
function COMMANDX.debug(cmd)
	local address = adjust_address(cmd[2])
	local agent = skynet.newservice "debug_agent"
	local stop
	local term_co = coroutine.running()
	local function forward_cmd()
		repeat
			-- notice :  It's a bad practice to call socket.readline from two threads (this one and console_main_loop), be careful.
			skynet.call(agent, "lua", "ping")	-- detect agent alive, if agent exit, raise error
			local cmdline = socket.readline(cmd.fd, "\n")
			cmdline = cmdline and cmdline:gsub("(.*)\r$", "%1")
			if not cmdline then
				skynet.send(agent, "lua", "cmd", "cont")
				break
			end
			skynet.send(agent, "lua", "cmd", cmdline)
		until stop or cmdline == "cont"
	end
	skynet.fork(function()
		pcall(forward_cmd)
		if not stop then	-- block at skynet.call "start"
			term_co = nil
		else
			skynet.wakeup(term_co)
		end
	end)
	local ok, err = skynet.call(agent, "lua", "start", address, cmd.fd)
	stop = true
	if term_co then
		-- wait for fork coroutine exit.
		skynet.wait(term_co)
	end

	if not ok then
		error(err)
	end
end

--[[
logon/logoff address 记录一个服务所有的输入消息到文件。需要在 Config 里配置 logpath 。
--]]
function COMMAND.logon(address)
	address = adjust_address(address)
	core.command("LOGON", skynet.address(address))
end

function COMMAND.logoff(address)
	address = adjust_address(address)
	core.command("LOGOFF", skynet.address(address))
end

-- signal address sig 向服务发送一个信号，sig 默认为 0 。
-- 当一个服务陷入死循环时，默认信号会打断正在执行的 lua 字节码，并抛出 error 显示调用栈。
-- 这是针对 endless loop 的 log 的有效调试方法。注：这里的信号并非系统信号。
-- skynet_module.h: skynet_module_instance_signal
-- 也就是说, C模块要实现 xxx_signal 接口
function COMMAND.signal(address, sig)
	address = skynet.address(adjust_address(address))
	if sig then
		core.command("SIGNAL", string.format("%s %d",address,sig))
	else
		core.command("SIGNAL", address)
	end
end

function COMMAND.cmem()
	local info = memory.info()
	local tmp = {}
	for k,v in pairs(info) do
		tmp[skynet.address(k)] = v
	end
	tmp.total = memory.total()
	tmp.block = memory.block()

	return tmp
end

-- ping address 检查某个服务是否阻塞
function COMMAND.ping(address)
	address = adjust_address(address)
	local ti = skynet.now()
	skynet.call(address, "debug", "PING")
	ti = skynet.now() - ti
	return tostring(ti)
end

local function toboolean(x)
	return x and (x == "true" or x == "on")
end

function COMMAND.trace(address, proto, flag)
	address = adjust_address(address)
	if flag == nil then
		if proto == "on" or proto == "off" then
			proto = toboolean(proto)
		end
	else
		flag = toboolean(flag)
	end
	skynet.call(address, "debug", "TRACELOG", proto, flag)
end

-- call address 调用一个服务的lua类型接口，格式为: call address "foo", arg1, ... 
-- 注意接口名和string型参数必须加引号,且以逗号隔开, address目前支持服务名方式。
function COMMANDX.call(cmd)
	local address = adjust_address(cmd[2])
	local cmdline = assert(cmd[1]:match("%S+%s+%S+%s(.+)") , "need arguments")
	local args_func = assert(load("return " .. cmdline, "debug console", "t", {}), "Invalid arguments")
	local args = table.pack(pcall(args_func))
	if not args[1] then
		error(args[2])
	end
	local rets = table.pack(skynet.call(address, "lua", table.unpack(args, 2, args.n)))
	return rets
end

local function bytes(size)
	if size == nil or size == 0 then
		return
	end
	if size < 1024 then
		return size
	end
	if size < 1024 * 1024 then
		return tostring(size/1024) .. "K"
	end
	return tostring(size/(1024*1024)) .. "M"
end

local function convert_stat(info)
	local now = skynet.now()
	local function time(t)
		if t == nil then
			return
		end
		t = now - t
		if t < 6000 then
			return tostring(t/100) .. "s"
		end
		local hour = t // (100*60*60)
		t = t - hour * 100 * 60 * 60
		local min = t // (100*60)
		t = t - min * 100 * 60
		local sec = t / 100
		return string.format("%s%d:%.2gs",hour == 0 and "" or (hour .. ":"),min,sec)
	end

	info.address = skynet.address(info.address)
	info.read = bytes(info.read)
	info.write = bytes(info.write)
	info.wbuffer = bytes(info.wbuffer)
	info.rtime = time(info.rtime)
	info.wtime = time(info.wtime)
end

-- netstat 列出网络连接的概况。
function COMMAND.netstat()
	local stat = socket.netstat()
	for _, info in ipairs(stat) do
		convert_stat(info)
	end
	return stat
end

function COMMAND.dumpheap()
	memory.dumpheap()
end

function COMMAND.profactive(flag)
	if flag ~= nil then
		if flag == "on" or flag == "off" then
			flag = toboolean(flag)
		end
		memory.profactive(flag)
	end
	local active = memory.profactive()
	return "heap profilling is ".. (active and "active" or "deactive")
end
