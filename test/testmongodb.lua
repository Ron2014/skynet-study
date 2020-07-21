-- config: config.mongodb
-- start: main_mongodb.lua
local skynet = require "skynet"
local mongo = require "skynet.db.mongo"
local bson = require "bson"

local host, port, db_name, username, password = ...
if port then
	port = math.tointeger(port)
end

print(host, port, db_name, username, password)

local function _create_client()
	return mongo.client(
		{
			host = host, port = port,
			username = username, password = password,
			authdb = db_name,
		}
	)
end

function test_auth()
	local c = mongo.client(
		{
			host = host, port = port,
		}
	)
	db = c[db_name]
	assert(db:auth(username, password), "auth error!")

	db.testdb:dropIndex("*")
	db.testdb:drop()

--[[
-- 由于json格式只有number类型，mongodb为了保证不出错将shell中所有的int、double类型都存为double。
-- 当然官方也考虑到用户实际想保存整型的问题，故允许在json中增加整型函数NumberInt()。

db.runCommand({
  insert: "testdb",
  documents:[
	  {test_key:NumberInt(1)}
	]
})

_id 类型为 ObjectId，自动生成的
]]

	-- 插入两条数据
	local ok, err, ret = db.testdb:safe_insert({test_key = 1});
	assert(ok and ret and ret.n == 1, err)

	local ok, err, ret = db.testdb:safe_insert({test_key = 1});
	assert(ok and ret and ret.n == 1, err)
end

--[[
	这里逻辑和 test_auth 函数是完全一样的。
	仅仅是用户验证封装到 _create_client
]]
function test_insert_without_index()
	local db = _create_client()

	db[db_name].testdb:dropIndex("*")
	db[db_name].testdb:drop()

	local ok, err, ret = db[db_name].testdb:safe_insert({test_key = 1});
	assert(ok and ret and ret.n == 1, err)

	local ok, err, ret = db[db_name].testdb:safe_insert({test_key = 1});
	assert(ok and ret and ret.n == 1, err)
end

--[[
	索引，unique key
	插入时重复key error
]]
function test_insert_with_index()
	local db = _create_client()

	db[db_name].testdb:dropIndex("*")
	db[db_name].testdb:drop()

	-- 添加了unique索引，插入时会有重复error
	db[db_name].testdb:ensureIndex({test_key = 1}, {unique = true, name = "test_key_index"})

	local ok, err, ret = db[db_name].testdb:safe_insert({test_key = 1})
	assert(ok and ret and ret.n == 1)

	-- 所以此处 ok == false
	local ok, err, ret = db[db_name].testdb:safe_insert({test_key = 1})
	assert(ok == false and string.find(err, "duplicate key error"))  
end

--[[
	复合索引
	查找
	删除
]]
function test_find_and_remove()
	local db = _create_client()

	db[db_name].testdb:dropIndex("*")
	db[db_name].testdb:drop()

	-- 复合索引，就算-1一样也是设置
	-- ensureIndex 最多接收2个参数, 最后那个table不起作用, 所以不会指定索引为unique
	-- db[db_name].testdb:ensureIndex({test_key = 1}, {test_key2 = -1}, {unique = true, name = "test_index"})
	-- db[db_name].testdb:ensureIndex({{test_key = 1}, {test_key2 = -1}, unique = true, name = "test_index"})
	db[db_name].testdb:ensureIndex("test_key", "test_key2", {unique = true, name = "test_index"})

	local ok, err, ret = db[db_name].testdb:safe_insert({test_key = 1, test_key2 = 1})
	assert(ok and ret and ret.n == 1, err)

	local ok, err, ret = db[db_name].testdb:safe_insert({test_key = 1, test_key2 = 2})
	assert(ok and ret and ret.n == 1, err)

	-- local ok, err, ret = db[db_name].testdb:safe_insert({test_key = 1, test_key2 = 2})
	-- assert(ok and ret and ret.n == 1, err)
	-- -- assert(ok == false and ret and ret.n == 0, err)		-- duplicate key

	local ok, err, ret = db[db_name].testdb:safe_insert({test_key = 2, test_key2 = 3})
	assert(ok and ret and ret.n == 1, err)

	local ret = db[db_name].testdb:findOne({test_key2 = 1})		-- 匹配第一条
	assert(ret and ret.test_key2 == 1, err)

	local ret = db[db_name].testdb:find({
		test_key2 = {['$lt'] = 100}
	}):sort({test_key = 1}, {test_key2 = -1}):skip(1):limit(1)
	-- 1 2 - skip
	-- 1 1 ---- limit 1
	-- 2 3

 	assert(ret:count() == 3)
 	assert(ret:count(true) == 1)
	if ret:hasNext() then
		ret = ret:next()
	end
	assert(ret and ret.test_key2 == 1)

	db[db_name].testdb:delete({test_key = 1})
	db[db_name].testdb:delete({test_key = 2})

	local ret = db[db_name].testdb:findOne({test_key = 1})
	assert(ret == nil)
end


--[[
	关于日期的测试
	赋予索引（data字段）一个期限，过期后自动删除
	实际删除数据的时间与索引加上数据指定的时间点之间存在偏移，官方解释自动删除的后台程序每60秒执行一次。

The TTL index does not guarantee that expired data will be deleted immediately upon expiration. There may be a delay between the time a document expires and the time that MongoDB removes the document from the database.

The background task that removes expired documents runs every 60 seconds. As a result, documents may remain in a collection during the period between the expiration of the document and the running of the background task.

Because the duration of the removal operation depends on the workload of your mongod instance, expired data may exist for some time beyond the 60 second period between runs of the background task.
]]
function test_expire_index()
	local db = _create_client()

	db[db_name].testdb:dropIndex("*")
	db[db_name].testdb:drop()

	db[db_name].testdb:ensureIndex({test_key = 1}, {unique = true, name = "test_key_index", expireAfterSeconds = 1, })
	db[db_name].testdb:ensureIndex({test_date = 1}, {expireAfterSeconds = 1, })

	local ok, err, ret = db[db_name].testdb:safe_insert({test_key = 1, test_date = bson.date(os.time())})
	assert(ok and ret and ret.n == 1, err)

	local ret = db[db_name].testdb:findOne({test_key = 1})
	assert(ret and ret.test_key == 1)

	for i = 1, 60 do
		skynet.sleep(100);
		print("check expire", i)
		local ret = db[db_name].testdb:findOne({test_key = 1})
		if ret == nil then
			return
		end
	end
	print("test expire index failed")
	assert(false, "test expire index failed");
end

--[[
	比较函数
]]
function test_compare()
	local db = _create_client()

	db[db_name].testdb:dropIndex("*")
	db[db_name].testdb:drop()

	db[db_name].testdb:ensureIndex({{type = 1}, {fighting = 1}})

	for i=1,2000 do
		local ok, err, ret = db[db_name].testdb:safe_insert({type = math.random(4), roleId = i, score = math.random(1, 1000), fighting = math.random(1,1000)})
		assert(ok and ret and ret.n == 1, err)
	end

	for i=2001, 4000 do
		local ok, err, ret = db[db_name].testdb:safe_insert({type = math.random(4), roleId = i, score = math.random(1, 1000)})
		assert(ok and ret and ret.n == 1, err)
	end

	local minFighting = 208
	local maxFighting = 503
	local roleId = 1049
	local type_ = 2
	local cursor = db[db_name].testdb:find({
		type = type_,
		roleId = {["$ne"] = roleId},
		fighting = {["$gte"] = minFighting, ["$lt"] = maxFighting},
	})

	while cursor:hasNext() do
		local node = cursor:next()
		print("roleId=", node.roleId, "fighting=", node.fighting, "score=", node.score)
	end
end

--[[
	过滤数据
	大小值比较
	字段是否存在
	字段是否为null
]]
function test_filter()
	local db = _create_client()

	db[db_name].testdb:dropIndex("*")
	db[db_name].testdb:drop()

	for i=1,2000 do
		db[db_name].testdb:safe_insert({roleId=i, type=math.random(4), score=math.random(1,1000), fighting=math.random(1,1000)})
	end

	for i=1,2000 do
		db[db_name].testdb:safe_insert({roleId=i, type=math.random(4), score=math.random(1,1000)})
	end

	local except_role = 1049
	local minFighting = 204
	local maxFighting = 508
	local type_ = 2
	local cursor = db[db_name].testdb:find({
		type = type_,
		roleId = { ["$ne"] = except_role },
		fighting = { ["$exists"] = true, ["$gte"] = minFighting, ["$lt"] = maxFighting},
	}, {roleId = 1, fighting = 1, score = 1}):sort({fighting=1, score=-1})

	while cursor:hasNext() do
		local node = cursor:next()
		print("roleId=", node.roleId, "fighting=", node.fighting, "score=", node.score)
	end
end

--[[
	正则表达式测试(模糊查找)
]]
function test_regex()
	local db = _create_client()

	db[db_name].testdb:dropIndex("*")
	db[db_name].testdb:drop()

	db[db_name].testdb:ensureIndex({{name = 1}})

	for i=1,10 do
		local ok, err, ret = db[db_name].testdb:safe_insert({name = string.format("TEST%08d", i)})
		assert(ok and ret and ret.n == 1, err)
	end

	-- 不支持 / ... / 的写法, 无法动态识别正则表达式
	-- { name = "/^TEST/" } 无效
	local cursor = db[db_name].testdb:find({name={["$regex"]="^TEST\\d{8}$"}}, {name = 1})
	-- local cursor = db[db_name].testdb:find({name="/^TEST\\d{8}$/"}, {name = 1})
	while cursor:hasNext() do
		local node = cursor:next()
		print("name=", node.name)
	end
end

--[[
	聚合查询
	$group
	$sort
	$limit
]]
function test_aggregate()
end

skynet.start(function()
	-- if username then
	-- 	print("Test auth")
	-- 	test_auth()
	-- end
	-- print("Test insert without index")
	-- test_insert_without_index()
	-- print("Test insert index")
	-- test_insert_with_index()
	print("Test find and remove")
	test_find_and_remove()
	-- print("Test aggregate")
	-- test_aggregate()
	-- print("Test find")
	-- test_filter()
	-- test_compare()
	-- print("Test regex")
	-- test_regex()
	-- print("Test expire index")
	-- test_expire_index()
	print("mongodb test finish.")
end)