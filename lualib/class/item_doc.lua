local attrdef = require "class.attrdef"
local bson = require "bson"

local itemdoc = {}
itemdoc.CollectionName = "Item"
itemdoc.Attr = {
	_id = {
		type = "objectid",
		default = nil,
		flag = attrdef.FLAG_FROM_DB + attrdef.FLAG_TARGET_DB,
		attr = "protected",
	},
	pid = {
		type = "int64",
		default = 0,
		flag = attrdef.FLAG_FROM_DB + attrdef.FLAG_TO_DB,
		attr = "protected",
	},
	host = {
		type = "int32",
		default = 0,
		flag = attrdef.FLAG_FROM_DB + attrdef.FLAG_TO_DB,
		attr = "protected",
	},
	idx = {
		type = "int32",
		default = 0,
		flag = attrdef.FLAG_FROM_DB + attrdef.FLAG_TO_DB,
		attr = "protected",
	},
	num = {
		type = "int32",
		default = 1,
		flag = attrdef.FLAG_FROM_DB + attrdef.FLAG_TO_DB,
		attr = "protected",
	},
	price = {
		type = "double",
		default = 19.26,
		flag = attrdef.FLAG_FROM_DB + attrdef.FLAG_TO_DB,
		attr = "protected",
	},
	valid = {
		type = "boolean",
		default = true,
		flag = attrdef.FLAG_FROM_DB + attrdef.FLAG_TO_DB,
		attr = "protected",
	},
	over = {
		type = "timestamp",
		default = 0,
		flag = attrdef.FLAG_FROM_DB + attrdef.FLAG_TO_DB,
		attr = "protected",
	},
	tm = {
		type = "date",
		default = 0,
		flag = attrdef.FLAG_FROM_DB + attrdef.FLAG_TO_DB,
		attr = "protected",
	},
}



return itemdoc