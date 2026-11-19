.pragma library

var characterFields = ["caption", "captionShifted", "symView", "symView2",
                       "accents", "accentsShifted"]
var keyTypes = ["character", "shift", "backspace", "symbol", "comma",
                "period", "space", "splitSpace", "enter"]

function accentsFor(caption, shifted) {
    var accents = {a: "aāáǎà", e: "eēéěè", i: "iīíǐì", o: "oōóǒò",
                   u: "uūúǔù", v: "vüǖǘǚǜ"}[caption.toLowerCase()] || ""
    return shifted ? accents.toUpperCase() : accents
}

function defaultLayout() {
    var letters = ["qwertyuiop", "asdfghjkl", "zxcvbnm"]
    var symbols = ["1234567890", "*#+-（）—…%", "“”；：、！？"]
    var symbols2 = [["@", "/", "\\", "~", "^", "_", "¥", "€", "$", "£"],
                    ["§", "=", "〈", "〉", "(", ")", "《", "》", "&"],
                    ["'", "\"", ";", ":", "·", "!", "?"]]
    var rows = []
    for (var row = 0; row < letters.length; ++row) {
        var keys = []
        if (row === 2)
            keys.push({type: "shift"})
        for (var index = 0; index < letters[row].length; ++index) {
            keys.push({type: "character", caption: letters[row][index],
                       symView: symbols[row][index], symView2: symbols2[row][index]})
        }
        if (row === 2)
            keys.push({type: "backspace"})
        rows.push({splitIndex: row === 1 ? 5 : -1, keys: keys})
    }
    rows.push({splitIndex: 3, keys: [{type: "symbol"}, {type: "comma"},
                                   {type: "space"}, {type: "splitSpace"},
                                   {type: "period"}, {type: "enter"}]})
    return {version: 1, rows: rows}
}

function validate(layout) {
    if (!layout || layout.version !== 1 || !Array.isArray(layout.rows))
        return "布局需要 version 为 1，并包含 rows 数组"
    if (layout.rows.length < 1 || layout.rows.length > 6)
        return "键盘需要 1—6 行按键"
    var required = {backspace: false, space: false, enter: false}
    for (var rowIndex = 0; rowIndex < layout.rows.length; ++rowIndex) {
        var row = layout.rows[rowIndex]
        if (!row || !Array.isArray(row.keys) || row.keys.length < 1 || row.keys.length > 20)
            return "每行需要 1—20 个按键"
        if (row.splitIndex !== undefined &&
                (typeof row.splitIndex !== "number" || row.splitIndex % 1 !== 0 ||
                 row.splitIndex < -1 || row.splitIndex > row.keys.length))
            return "splitIndex 需要为 -1 或该行的分割位置"
        if (row.separateButtonSizes !== undefined && typeof row.separateButtonSizes !== "boolean")
            return "separateButtonSizes 需要为布尔值"
        for (var keyIndex = 0; keyIndex < row.keys.length; ++keyIndex) {
            var key = row.keys[keyIndex]
            if (!key || keyTypes.indexOf(key.type) < 0)
                return "存在不支持的按键类型"
            if (required.hasOwnProperty(key.type))
                required[key.type] = true
            if (key.type === "character" &&
                    (typeof key.caption !== "string" || key.caption.length < 1 || key.caption.length > 8))
                return "字符键需要 1—8 个字符的 caption"
            for (var fieldIndex = 0; fieldIndex < characterFields.length; ++fieldIndex) {
                var field = characterFields[fieldIndex]
                if (key.type !== "character" && field !== "caption" && key[field] !== undefined)
                    return "该按键类型只支持 caption 字符属性"
                if (key[field] !== undefined &&
                        (typeof key[field] !== "string" || key[field].length > 64))
                    return "按键字符属性需要为不超过 64 个字符的字符串"
            }
            if (key.implicitWidth !== undefined &&
                    (typeof key.implicitWidth !== "number" || !isFinite(key.implicitWidth) ||
                     key.implicitWidth <= 0 || key.implicitWidth > 2000))
                return "implicitWidth 需要为 1—2000 的按键宽度"
            if (key.fixedWidth !== undefined &&
                    (key.type !== "character" || typeof key.fixedWidth !== "boolean"))
                return "fixedWidth 只适用于字符键，需要为布尔值"
        }
    }
    if (!required.backspace || !required.space || !required.enter)
        return "布局需要包含退格、空格和回车键"
    return ""
}

function decode(text) {
    if (!text || text.trim().length === 0)
        return null
    var layout = JSON.parse(text)
    var error = validate(layout)
    if (error.length > 0)
        throw new Error(error)
    return layout
}

function keyProperties(key) {
    var properties = {}
    var fields = characterFields.concat(["implicitWidth", "fixedWidth"])
    for (var index = 0; index < fields.length; ++index) {
        var field = fields[index]
        if (key[field] !== undefined)
            properties[field] = key[field]
    }
    return properties
}

function withLetterRows(first, second, third) {
    var letters = [first.toLowerCase(), second.toLowerCase(), third.toLowerCase()]
    if (letters[0].length !== 10 || letters[1].length !== 9 || letters[2].length !== 7)
        throw new Error("快捷编辑的三行需要分别包含 10、9、7 个字母")
    if (letters.join("").split("").sort().join("") !== "abcdefghijklmnopqrstuvwxyz")
        throw new Error("快捷编辑需要包含全部 26 个英文字母，每个字母出现一次")
    var layout = defaultLayout()
    for (var row = 0; row < letters.length; ++row) {
        var offset = row === 2 ? 1 : 0
        for (var index = 0; index < letters[row].length; ++index)
            layout.rows[row].keys[index + offset].caption = letters[row][index]
    }
    return layout
}
