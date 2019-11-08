local args = {}
for word in string.gmatch(..., "%S+") do
	table.insert(args, word)
end

SERVICE_NAME = args[1]										-- ./service/?.lua （默认）

local main, pattern

-- 加载lua服务文件，将其转换成main函数
local err = {}
for pat in string.gmatch(LUA_SERVICE, "([^;]+);*") do
	local filename = string.gsub(pat, "?", SERVICE_NAME)	-- ?替换成文件名
	local f, msg = loadfile(filename)
	if not f then
		table.insert(err, msg)
	else
		pattern = pat										-- pattern记录下与文件目录对应的搜索路径
		main = f
		break
	end
end

if not main then
	-- 找不到的话，就列出搜索路径
	error(table.concat(err, "\n"))
end

-- 通过环境变量修改package路径
LUA_SERVICE = nil
package.path , LUA_PATH = LUA_PATH
package.cpath , LUA_CPATH = LUA_CPATH

-- 对于服务是个目录的情况，如 ./service/?/init.lua
-- 从pattern获取服务所在的文件目录 ./service/?/
-- 与服务名拼接得到 ./service/xxx/
-- 把 ./service/xxx/?.lua; 加到package路径中
local service_path = string.match(pattern, "(.*/)[^/?]+$")
if service_path then
	service_path = string.gsub(service_path, "?", args[1])
	package.path = service_path .. "?.lua;" .. package.path
	SERVICE_PATH = service_path
else
	local p = string.match(pattern, "(.*/).+$")
	SERVICE_PATH = p
end

-- preload 由 config 指定 (lua文件)
-- 在执行lua服务之前调用一次。见 ./examples/preload.lua
if LUA_PRELOAD then
	local f = assert(loadfile(LUA_PRELOAD))
	f(table.unpack(args))
	LUA_PRELOAD = nil
end

-- 第一个参数（lua服务文件名）已经放到 SERVICE_NAME 里了
-- 如果还有后续参数，就是这个这个lua服务的参数了
main(select(2, table.unpack(args)))
