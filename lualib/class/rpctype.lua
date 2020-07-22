local bson = require "bson"
local rpctype = {}

rpctype.serialize = function(obj, k)
    local inner = string.format("%s__", k)
    local attr = obj.Attr[k]
    local rt = rpctype[attr.type]
    local val = obj[inner]
    return rt._serialize(val)
end

rpctype.int32 = {
    _check = function(data)
        local ty, val = bson.type(data)
        if ty ~= "integer" then return false end
        return true
    end,
    _deserialize = function(data)
        local ty, val = bson.type(data)
        return val
    end,
    _serialize = function(data)
        return data
    end,
}

rpctype.int64 = {
    _check = function(data)
        local ty, val = bson.type(data)
        if ty ~= "integer" then return false end
        return true
    end,
    _deserialize = function(data)
        local ty, val = bson.type(data)
        return val
    end,
    _serialize = function(data)
        return data
    end,
}

rpctype.double = {
    _check = function(data)
        local ty, val = bson.type(data)
        if ty ~= "double" then return false end
        return true
    end,
    _deserialize = function(data)
        local ty, val = bson.type(data)
        return val
    end,
    _serialize = function(data)
        return data
    end,
}

rpctype.objectid = {
    _check = function(data)
        local ty, val = bson.type(data)
        if ty ~= "objectid" then return false end
        return true
    end,
    _deserialize = function(data)
        local ty, val = bson.type(data)
        return val
    end,
    _serialize = function(data)
        if data == nil then
            return bson.objectid()
        end
        return data
    end,
}

rpctype.boolean = {
    _check = function(data)
        local ty, val = bson.type(data)
        if ty ~= "boolean" then return false end
        return true
    end,
    _deserialize = function(data)
        local ty, val = bson.type(data)
        return val
    end,
    _serialize = function(data)
        return data
    end,
}

rpctype.string = {
    _check = function(data)
        local ty, val = bson.type(data)
        if ty ~= "string" then return false end
        return true
    end,
    _deserialize = function(data)
        local ty, val = bson.type(data)
        return val
    end,
    _serialize = function(data)
        return data
    end,
}

rpctype.timestamp = {
    _check = function(data)
        local ty, val = bson.type(data)
        if ty ~= "timestamp" then return false end
        return true
    end,
    _deserialize = function(data)
        local ty, val = bson.type(data)
        return val
    end,
    _serialize = function(data)
        return bson.timestamp(data)
    end,
}

rpctype.date = {
    _check = function(data)
        local ty, val = bson.type(data)
        if ty ~= "date" then return false end
        return true
    end,
    _deserialize = function(data)
        local ty, val = bson.type(data)
        return val
    end,
    _serialize = function(data)
        return bson.date(data)
    end,
}

rpctype.item = {
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