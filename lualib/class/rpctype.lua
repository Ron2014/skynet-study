local rpctype = {}

rpctype.serialize = function(self, obj, k)
    local inner = string.format("%s__", k)
    local attr = obj.Attr[k]
    local rt = rpctype[attr.type]
    local val = obj[inner]
    return rt:_serialize(val)
end

rpctype.double = {
    _deserialize = function(self, data)
        return data
    end,
    _serialize = function(self, data)
        return data
    end,
}

rpctype.int = {
    _deserialize = function(self, data)
        return data
    end,
    _serialize = function(self, data)
        return data
    end,
}

rpctype.string = {
    _deserialize = function(self, data)
        return data
    end,
    _serialize = function(self, data)
        return data
    end,
}

rpctype.array = {
    _deserialize = function(self, data)
        return data
    end,
    _serialize = function(self, data)
        return data
    end,
}

rpctype.item = {
    _deserialize = function(self, data)
        local Item = require("class.item_t")
        local obj = Item.new()
        obj:deserialize(data)
        return obj
    end,
    _serialize = function(self, obj)
        return obj:serialize()
    end,
}