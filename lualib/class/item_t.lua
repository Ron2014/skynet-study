local itemdoc = require("class.item_doc")
local struct = require("class.struct_t")
local Item = class("Item", struct)
makeAttr(Item, itemdoc.Attr, itemdoc.CollectionName)

return Item