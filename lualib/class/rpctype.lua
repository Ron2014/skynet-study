local bson = require "bson"
local skynet = require "skynet"
local bson_type = bson.type
local math_type = math.type
local rpctype = {}

rpctype.serialize = function(obj, k)
    local inner = string.format("%s__", k)
    local attr = obj.Attr[k]
    local rt = rpctype[attr.type]
    local val = obj[inner]
    return rt._serialize(val)
end

rpctype.deserialize = function(obj, k, val)
    local v = obj.Attr[k]
    assert(v, string.format("%s:%s is not property of %s", k, type(val), obj.class.__cname))

    local rt = rpctype[v.type]
    if rt._check and not rt._check(val) then
        error(string.format("bad argument %s, expected %s, but got %s", k, v.type, type(val)))
    end

    local inner = string.format("%s__", k)
    obj[inner] = rt._deserialize(val)
end

rpctype.load = function(obj, k, val)
    local v = obj.Attr[k]
    assert(v, string.format("%s:%s is not property of %s", k, type(val), obj.class.__cname))

    local rt = rpctype[v.type]
    if rt._check and not rt._check(val) then
        error(string.format("bad argument %s, expected %s, but got %s", k, v.type, type(val)))
    end

    local inner = string.format("%s__", k)
    obj[inner] = rt._deserialize(val)
end

rpctype.objectid = {
    _check = function(data)
        local ty, val = bson_type(data)
        if ty ~= "objectid" then return false end
        return true
    end,
    _deserialize = function(data)
        return data
    end,
    _serialize = function(data)
        if data == nil then
            return bson.objectid()
        end
        return data
    end,
}

rpctype.int = {
    _check = function(data)
        local ty, val = bson_type(data)
        if ty ~= "number" then return false end
        if math_type(val) ~= "integer" then return false end
        return true
    end,
    _deserialize = function(data)
        local ty, val = bson_type(data)
        return val
    end,
    _serialize = function(data)
        return data
    end,
}

rpctype.double = {
    -- can save as integer
    _check = function(data)
        local ty, val = bson_type(data)
        if ty ~= "number" then return false end
        -- if math_type(val) ~= "float" then return false end
        return true
    end,
    _deserialize = function(data)
        local ty, val = bson_type(data)
        return val
    end,
    _serialize = function(data)
        return data
    end,
}

rpctype.boolean = {
    _check = function(data)
        local ty, val = bson_type(data)
        if ty ~= "boolean" then return false end
        return true
    end,
    _deserialize = function(data)
        local ty, val = bson_type(data)
        return val
    end,
    _serialize = function(data)
        return data
    end,
}

rpctype.string = {
    _check = function(data)
        local ty, val = bson_type(data)
        if ty ~= "string" then return false end
        return true
    end,
    _deserialize = function(data)
        local ty, val = bson_type(data)
        return val
    end,
    _serialize = function(data)
        return data
    end,
}

rpctype.timestamp = {
    _check = function(data)
        local ty, val = bson_type(data)
        if ty ~= "timestamp" then return false end
        return true
    end,
    _deserialize = function(data)
        local ty, val = bson_type(data)
        return val
    end,
    _serialize = function(data)
        if data == nil then
            return bson.timestamp(skynet.timestamp())
        end
        return bson.timestamp(data)
    end,
}

rpctype.date = {
    _check = function(data)
        local ty, val = bson_type(data)
        if ty ~= "date" then return false end
        return true
    end,
    _deserialize = function(data)
        local ty, val = bson_type(data)
        return val
    end,
    _serialize = function(data)
        if data == nil then
            return bson.date(skynet.timestamp())
        end
        return bson.date(data)
    end,
}

rpctype.item = {
    _load = function(data)
        local Item = require("class.item_t")
        local obj = Item.new()
        obj:load(data)
        return obj
    end,

    _save = function(obj)
        return obj:save()
    end,

    _deserialize = function(data)
        local Item = require("class.item_t")
        local obj = Item.new()
        obj:deserialize(data)
        return obj
    end,

    _serialize = function(obj)
        return obj:serialize()
    end,
}

return rpctype