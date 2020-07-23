local struct = require("class.struct_t")
local rpctype = require("class.rpctype")

local Array = class("Array", struct)

function Array:__newindex(key, val)
    if type(key) ~= 'number' then
        rawset(self, key, val)
        return
    end

    assert(self:checkEntry(val))

    local old = self.data_[key]
    self.data_[key] = val

    if old ~= val then
        self:mark(key)
    end
end

function Array:__index(key)
    if type(key) == 'number' then
        return self.data_[key]
    end
    return Array[key]
end

function Array:ctor(root, attr)
    Array.super.ctor(self, root, attr)

    local attr = root.Attr[attr]
    assert(not attr.subtype or rpctype[attr.subtype], "[Array:init] rpctype required, got unknown type, " .. attr.subtype )
    
    self.type_ = attr.subtype or ""
    self.data_ = {}
end

function Array:check(obj)
    return iskindof(obj, Array.__cname) and self:type() == obj:type()
end

function Array:checkEntry(entry)
    local rt = rpctype[self.type_]
    if rt and rt._check then
        return rt._check(entry)
    end
    return true
end

function Array:serialize()
    local data = {}
    for k, v in pairs(self.data_) do
        local rt = rpctype[self.type_]
        if rt then v = rt._serialize(v) end
        table.insert(data, k)
        table.insert(data, v)
    end
    return data
end

function Array:deserialize(data)
    local len = #data
    for i=1,len,2 do
        local k = data[i]
        local v = data[i+1]
        local rt = rpctype[self.type_]
        if rt then v = rt._deserialize(v, self, k) end
        self.data_[k] = v
    end
end

function Array:type()
    return self.type_
end


return Array