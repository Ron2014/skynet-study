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
            
            if bit32.bind(v.flag, attrdef.FLAG_TARGET_DB) ~= 0 then
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
    local settor = {}
    for k, _ in pairs(self.markAttr_) do
        settor[k] = rpctype:serialize(self, k)
    end
    self.markAttr_ = {}
    
    -- target
    local target = {}
    for _, k in ipairs(self.Target) do
		target[k] = rpctype:serialize(self, k)
    end

    -- lua class
    -- collection_name
    -- send to db.collector.update(target, settor)
end

function struct:load(data)
end

function struct:deserialize(data)
end

function struct:serialize()
end

return struct