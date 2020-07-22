local attrdef = require "class.attrdef"

local itemdoc = {}
itemdoc.CollectionName = "Item"
itemdoc.Attr = {
	_id = {
		type = "objectid",
		default = nil,
		flag = attrdef.FLAG_FROM_DB + attrdef.FLAG_TARGET_DB,
		attr = "public",
	},
	pid = {
		type = "int",
		default = 0,
		flag = attrdef.FLAG_FROM_DB + attrdef.FLAG_TO_DB,
		attr = "public",
	},
	host = {
		type = "int",
		default = 0,
		flag = attrdef.FLAG_FROM_DB + attrdef.FLAG_TO_DB,
		attr = "public",
	},
	idx = {
		type = "int",
		default = 0,
		flag = attrdef.FLAG_FROM_DB + attrdef.FLAG_TO_DB,
		attr = "public",
	},
	num = {
		type = "int",
		default = 1,
		flag = attrdef.FLAG_FROM_DB + attrdef.FLAG_TO_DB,
		attr = "public",
	},
	price = {
		type = "double",
		default = 19.26,
		flag = attrdef.FLAG_FROM_DB + attrdef.FLAG_TO_DB,
		attr = "public",
	},
	valid = {
		type = "boolean",
		default = true,
		flag = attrdef.FLAG_FROM_DB + attrdef.FLAG_TO_DB,
		attr = "public",
	},
	over = {
		type = "timestamp",
		default = nil,
		flag = attrdef.FLAG_FROM_DB + attrdef.FLAG_TO_DB,
		attr = "public",
	},
	tm = {
		type = "date",
		default = nil,
		flag = attrdef.FLAG_FROM_DB + attrdef.FLAG_TO_DB,
		attr = "public",
	},
}



return itemdoc