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
                if v.flag and bit32.band(v.flag, attrdef.FLAG_CACHED) ~= 0 and self:_clean(k) then
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
            else
                if self[inner] == nil then
                    -- need default ?
                    if rpctype[v.type]._default then
                        if v.default then
                            self[inner] = rpctype[v.type]._default(self, k)
                            self:mark(k)
                        end
                    else
                        if v.default then
                            self[inner] = deepcopy(v.default)
                            self:mark(k)
                        end
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

function struct:ctor(root, sn)
    -- attr name from parent
    -- where I can mark when myself in root 
    self.root_ = root
    self.sn_ = sn

    self.markAttr_ = {}
    self.dirtyAttr_ = {}
end

function struct:root()
    return self.root_
end

function struct:sn(val)
    if val then
        self.sn_ = val
    end
    return self.sn_
end

function struct:mark(key)
    self.markAttr_[key] = true

    local root = self.root_
    if root then
        root:mark(self:sn())
    end
end

function struct:_dirty(key)
    self.dirtyAttr_[key] = true
end

function struct:_clean(key)
    local ret = self.dirtyAttr_[key]
    self.dirtyAttr_[key] = nil
    return ret
end

-- for FROM_DB property
-- allow sync partly
function struct:load(data)
    for k, val in pairs(data) do
        assert(self.Selector[k], string.format("%s:%s is not FROM_DB of %s", k, type(val), self.class.__cname))
        rpctype.deserialize(self, k, val)
    end
end

-- for TO_DB property
function struct:save()
    -- clear all _dirty
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

    return settor, target
end

-- for all property
function struct:deserialize(data)
    for k, val in pairs(data) do
        rpctype.deserialize(self, k, val)
    end
end

function struct:serialize()
    local data = {}
    for k, v in pairs(self.Attr) do
        if v.attr == "public" then
            local rt = rpctype[v.type]
            local val = self[k](self)
            if val then data[k] = rt._serialize(val) end
        end
    end
    return data
end

return struct