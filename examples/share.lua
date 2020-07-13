-- 1. ./skynet example/config
-- 2. share
-- 利用 console 服务对 stdin 的处理来启动该服务
local skynet = require "skynet"
local sharedata = require "skynet.sharedata"	-- 1. suspend

local mode = ...

if mode == "host" then

skynet.start(function()
	skynet.error("new foobar")
	sharedata.new("foobar", { a=1, b= { "hello",  "world" } })	-- 3. suspend

	skynet.fork(function()
		skynet.sleep(200)	-- sleep 2s				5. suspend
		skynet.error("update foobar a = 2")
		sharedata.update("foobar", { a =2 })		--
		skynet.sleep(200)	-- sleep 2s
		skynet.error("update foobar a = 3")
		sharedata.update("foobar", { a = 3, b = { "change" } })
		skynet.sleep(100)
		skynet.error("delete foobar")
		sharedata.delete "foobar"
	end)
end)

else

-- SERVICE_NAME loader.lua 注册的
-- 值就是服务名, 在这里就是 share
skynet.start(function()
	skynet.newservice(SERVICE_NAME, "host")		-- 2. suspend

	local obj = sharedata.query "foobar"		-- 4. suspend

	local b = obj.b
	skynet.error(string.format("a=%d", obj.a))

	for k,v in ipairs(b) do
		skynet.error(string.format("b[%d]=%s", k,v))
	end

	-- test lua serialization
	local s = skynet.packstring(obj)
	local nobj = skynet.unpack(s)
	for k,v in pairs(nobj) do
		skynet.error(string.format("nobj[%s]=%s", k,v))
	end
	for k,v in ipairs(nobj.b) do
		skynet.error(string.format("nobj.b[%d]=%s", k,v))
	end

	for i = 1, 5 do
		skynet.sleep(100)						-- 6. suspend
		skynet.error("second " ..i)
		for k,v in pairs(obj) do
			skynet.error(string.format("%s = %s", k , tostring(v)))
		end
	end

	local ok, err = pcall(function()
		local tmp = { b[1], b[2] }	-- b is invalid , so pcall should failed
	end)

	if not ok then
		skynet.error(err)
	end

	-- obj. b is not the same with local b
	for k,v in ipairs(obj.b) do
		skynet.error(string.format("b[%d] = %s", k , tostring(v)))
	end

	collectgarbage()
	skynet.error("sleep")
	skynet.sleep(100)
	b = nil
	collectgarbage()
	skynet.error("sleep")
	skynet.sleep(100)

	skynet.exit()
end)

end
