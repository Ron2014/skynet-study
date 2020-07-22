local attrdef = require("class.attrdef")

local itemdoc = {}
itemdoc.CollectionName = "Item"
itemdoc.Attr = {
	sn = {
		type = "string",
		default = "0",
		flag = attrdef.FLAG_FROM_DB + attrdef.FLAG_TARGET_DB,
		attr = "protected",
	},
	id = {
		type = "int",
		default = 0,
		flag = attrdef.FLAG_FROM_DB + attrdef.FLAG_TO_DB,
		attr = "protected",
	},
	pid = {
		type = "int",
		default = 0,
		flag = attrdef.FLAG_FROM_DB + attrdef.FLAG_TO_DB,
		attr = "protected",
	},
	host = {
		type = "int",
		default = 0,
		flag = attrdef.FLAG_FROM_DB + attrdef.FLAG_TO_DB,
		attr = "protected",
	},
	idx = {
		type = "int",
		default = 0,
		flag = attrdef.FLAG_FROM_DB + attrdef.FLAG_TO_DB,
		attr = "protected",
	},
	num = {
		type = "int",
		default = 1,
		flag = attrdef.FLAG_FROM_DB + attrdef.FLAG_TO_DB,
		attr = "protected",
	},
	-- GET_TIME		over > 0			有效期
	-- USE_TIME		over == 0 / > 0		有效期
	-- REMAIN_TIME	over == 0 / > 0		剩余时间
	over = {
		type = "int",
		default = 0,
		flag = attrdef.FLAG_FROM_DB + attrdef.FLAG_TO_DB,
		attr = "protected",
	},
	val = {
		type = "VecInt",
		default = {},
		flag = attrdef.FLAG_FROM_DB + attrdef.FLAG_TO_DB,
		attr = "protected",
	},
	property = {
		type = "string",
		default = "",
		flag = attrdef.FLAG_FROM_DB + attrdef.FLAG_TO_DB,
		attr = "protected",
	},
}



return itemdoc