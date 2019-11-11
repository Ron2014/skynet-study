local skynet = require "skynet"
local harbor = require "skynet.harbor"
require "skynet.manager"	-- import skynet.launch, ...

--[[
	起一个lua虚拟机，将 .launcher 服务跑起来
	后面看情况 skynet.newservice 了几个服务，都是通过 .launcher 跑起来的
	如果 harbor 为 0，skynet 工作在单节点模式下。
		cdummy.lua服务，别名 .cslave
	有服务器ID
		如果是standalone，开启cmaster.lua服务
		cslave.lua服务，别名 .cslave
	对于 standalone
		datacenterd.lua服务，别名 DATACENTER
	service_mgr服务
	config->start指定的服务，定制的 skynet 节点的主程序。如 ./examples/main.lua

	snlua_init: 添加消息
	-> launch_cb -> init_cb: 处理消息 loader.lua 执行 bootstrap.lua

	snlua_init: 添加消息
	-> launch_cb -> init_cb: 处理消息 loader.lua 执行 launcher.lua
	之后lua服务的启动都交由 .launcher服务完成，包装成 skynet.newservice 调用

	需要进一步了解的内容：
	1. master-slave 集群架构
	2. datacenterd 数据中心
	3. cdummy 单节点模式
	4. service_mgr(.service) 服务管理器与 .launcher 服务的区别

	由此可见，bootstrap的作用是【构建服务器的运行时框架】
--]]

skynet.start(function()
	--[[
		如果把这个 skynet 进程作为主进程启动（skynet 可以由分布在多台机器上的多个进程构成网络），
		那么需要配置 standalone 这一项，表示这个进程是主节点，它需要开启一个控制中心，监听一个端口，让其它节点接入。
	--]]
	local standalone = skynet.getenv "standalone"

	local launcher = assert(skynet.launch("snlua","launcher"))
	skynet.name(".launcher", launcher)

	--如果没有配置服务器ID，必定是 standalone
	local harbor_id = tonumber(skynet.getenv "harbor" or 0)
	if harbor_id == 0 then
		assert(standalone ==  nil)
		standalone = true
		skynet.setenv("standalone", "true")

		local ok, slave = pcall(skynet.newservice, "cdummy")
		if not ok then
			skynet.abort()
		end
		skynet.name(".cslave", slave)

	else
		if standalone then
			if not pcall(skynet.newservice,"cmaster") then
				skynet.abort()
			end
		end

		local ok, slave = pcall(skynet.newservice, "cslave")
		if not ok then
			skynet.abort()
		end
		skynet.name(".cslave", slave)
	end

	if standalone then
		local datacenter = skynet.newservice "datacenterd"
		skynet.name("DATACENTER", datacenter)
	end
	skynet.newservice "service_mgr"
	pcall(skynet.newservice, skynet.getenv "start" or "main")

	skynet.exit()		-- 自举就是如此，事了拂衣去，深藏功与名。把该启动的都启动后，就自杀了，像盘古一样。
end)
