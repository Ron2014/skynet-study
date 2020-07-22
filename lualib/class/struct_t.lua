local attrdef = require("class.attrdef")
local rpctype = require("class.rpctype")
local bit32 = _G.bit32

function makeAttr(class, attr, collection)
    local selector = {}
    local target = {}
    
    for k, v in sortpairs(attr)do
        if v.flag then
            if bit32.band(v.flag, attrdef.FLAG_FROM_DB) ~= 0 then
                selector[k] = 1 -- selector for loading
            end
            
            if bit32.band(v.flag, attrdef.FLAG_TARGET_DB) ~= 0 then
                table.insert(target, k)   -- target for update
            end
		end

		class[k] = function(self, val)
            local inner = string.format("%s__", k)

            if val == nil then
                -- cached: update when accessed
                if v.flag and bit32.band(v.flag, attrdef.FLAG_CACHED) ~= 0 and self:cleardirty(k) then
                    local old = self[inner]
                    local func = self[string.format("up_%s", k)]
                    val = func(self, old)
                end
            end
            
			if val ~= nil then
                local old = self[inner]
                self[inner] = val
                
				if old ~= val then
					if v.flag and bit32.band(v.flag, attrdef.FLAG_TO_DB) ~= 0 then
						self:mark(k)
                    end

					local func = self[string.format("on_%s", k)]
					if func then
						func(self, old)
                    end
                end
            end

			return self[inner]
        end
    end

    class.Attr = attr
    class.Collection = collection
    class.Selector = selector
    class.Target = target
end

local struct = class("struct")

function struct:ctor()
	for k, v in pairs(self.Attr) do
		local inner = string.format("%s__", k)
		self[inner] = deepcopy(v.default)
    end
    
    self.markAttr_ = {}
    self.dirtyAttr_ = {}
end

function struct:mark(key)
    self.markAttr_[key] = true
end

function struct:dirty(key)
    self.dirtyAttr_[key] = true
end

function struct:cleardirty(key)
    local ret = self.dirtyAttr_[key]
    self.dirtyAttr_[key] = nil
    return ret
end

-- for db
function struct:save()
    -- clear all dirty
    for k, _ in pairs(self.dirtyAttr_) do
        local inner = string.format("%s__", k)
        local func = self[string.format("up_%s", k)]
        local val = func(self, self[inner])
        self[k](self, val)
    end
    self.dirtyAttr_ = {}
    
    -- settor from markAttr_
    local settor = nil
    for k, _ in pairs(self.markAttr_) do
        settor = settor or {}
        settor[k] = rpctype.serialize(self, k)
    end
    self.markAttr_ = {}
    
    -- target
    local target = {}
    for _, k in ipairs(self.Target) do
		target[k] = rpctype.serialize(self, k)
    end

    return target, settor
end

function struct:deserialize(data)
    for k, v in pairs(self.Attr) do
        local rt = rpctype[v.type]
        local inner = string.format("%s__", k)
        local val = data[k]
        if rt._check and not rt._check(val) then
            error(string.format("bad argument %s, expected %s, but got %s", k, v.type, type(val)))
        end
        self[inner] = rt._deserialize(val)
    end
end

function struct:serialize()
    local data = {}
    for k, v in pairs(self.Attr) do
        local rt = rpctype[v.type]
        local val = self[k](self)
        data[k] = rt._serialize(val)
    end
    return data
end

return struct