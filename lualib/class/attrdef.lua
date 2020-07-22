local attrdef = {}

attrdef.FLAG_TO_DB 		= 0x0001
attrdef.FLAG_FROM_DB 	= 0x0002
attrdef.FLAG_TARGET_DB  = 0x0004
attrdef.FLAG_CACHED  	= 0x0008

function copy(object)
    local lookup_table = {}
    local function _copy(object)
        if type(object) ~= "table" then
            return object
        elseif lookup_table[object] then
            return lookup_table[object]
        end
        local new_table = {}
        lookup_table[object] = new_table
        for index, value in pairs(object) do
            new_table[_copy(index)] = _copy(value)
        end
        return setmetatable(new_table, getmetatable(object))
    end
    return _copy(object)
end

deepcopy = copy

function class(classname, super)
    local cls

	if super then
		cls = setmetatable({}, {__index = super})
		cls.super = super
	else
		cls = {ctor = function() end}
	end

	cls.__cname = classname
	cls.__ctype = 2 -- lua
	cls.__index = cls

	function cls.new(...)
		local instance = setmetatable({}, cls)
        instance.class = cls
        instance:ctor(...)
		return instance
	end

   return cls
end

function iskindof(obj, className)
    local t = type(obj)

    if t == "table" then
        local mt = getmetatable(obj)
        while mt and mt.__index do
            if mt.__index.__cname == className then
                return true
            end
            mt = mt.super
        end
        return false

    elseif t == "userdata" then

    else
        return false
    end
end

function sortpairs(t, inc)
	local inc = inc
	if inc==nil then inc=true end

	local arrayt = {}
	local i=1
	for k,v in pairs(t) do
		arrayt[i]={k,v}
		i=i+1
	end
	table.sort( arrayt, function( a, b )
		local w1, w2 = 0, 0
		local rate = (inc and 1) or -1
		if a[1]<b[1] then
			w1 = w1 + rate
		end
		if a[1]>b[1] then
			w2 = w2 + rate
		end
		return w1 > w2
	end )

	i=0
	return function()
		i=i+1
		local e=arrayt[i]
		if e then
			return e[1],e[2]
		else
			return nil,nil
		end
	end
end

return attrdef