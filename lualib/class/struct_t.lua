local attrdef = require("class.attrdef")
local rpctype = require("class.rpctype")
local bit32 = _G.bit32

function makeAttr(class, attr, collection)
    local selector = {}
    local target = {}
    
    for k, v in sortpairs(attr)do
        if v.flag then
            if bit32.band(v.flag, attrdef.FLAG_FROM_DB) ~= 0 then
                -- table.insert(selector, k) -- selector for loading
                selector[k] = 1
            end
            if bit32.bind(v.flag, attrdef.FLAG_TARGET_DB) ~= 0 then
                table.insert(target, k)   -- target for update
            end
		end

		class[k] = function(self, val)
            local inner = string.format("%s__", k)
            
			if val then
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
end

function struct:mark(key)
    self.markAttr_[key] = true
end

function struct:load(data)
end

function struct:save()
    local settor = {}
    for k, _ in pairs(self.markAttr_) do
        settor[k] = rpctype:serialize(self, k)
    end
    
    local query = {}
    for _, k in ipairs(self.Target) do
		query[k] = rpctype:serialize(self, k)
    end

    -- send to db.collector.update(query, settor)
end

function struct:deserialize(data)
end

function struct:serialize()
end

return struct