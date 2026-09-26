-- Export a snapshot; KOReader remains the source of truth for synced notes.
local Notes = {}
local function text(value)
    return type(value) == "string" and value:gsub("\r\n", "\n"):gsub("\r", "\n"):gsub("%z", "") or ""
end
local function escape(value)
    return (text(value):gsub("([\\`*_{}%[%]<>#|!])", "\\%1"))
end
local function inline(value) return (escape(value):gsub("\n", " ")) end

function Notes.filename(path)
    local md5 = require("ffi/sha2").md5
    local stem = (path:match("([^/]+)$") or "Book"):gsub("%.[^.]+$", "")
    stem = stem:gsub("[^%w%-_]", "_"):sub(1, 60)
    return stem .. "-" .. md5(path):sub(1, 16) .. ".md"
end

function Notes.annotations(settings, live)
    if type(live) == "table" then return live end
    local modern = settings:readSetting("annotations")
    if type(modern) == "table" then return modern end
    local result = {}
    local bookmarks = settings:readSetting("bookmarks") or {}
    for page, highlights in pairs(settings:readSetting("highlight") or {}) do
        for _, item in ipairs(highlights) do
            local note = item.note
            for _, bookmark in ipairs(bookmarks) do
                if item.datetime and bookmark.datetime == item.datetime then
                    note = bookmark.note or (bookmark.text ~= item.text and bookmark.text or note)
                    break
                end
            end
            result[#result + 1] = { drawer = item.drawer or "lighten", text = item.text, note = note,
                chapter = item.chapter, pageno = page, datetime = item.datetime }
        end
    end
    table.sort(result, function(a, b) return tostring(a.pageno) < tostring(b.pageno) end)
    return result
end

function Notes.write(output, title, author, annotations)
    local file, err = io.open(output, "wb")
    if not file then return false, err end
    local count = 0
    local function write(value)
        local ok, reason = file:write(value)
        if not ok then error(reason or "Could not write notes") end
    end
    local ok, reason = pcall(function()
        write("# " .. inline(title) .. "\n\n")
        if author and author ~= "" then write(inline(author) .. "\n\n") end
        write("Synced from KOReader. Sending notes again replaces this copy.\n\n")
        for _, item in ipairs(annotations) do
            local quote, note = text(item.text), text(item.note)
            if (item.drawer and quote ~= "") or note ~= "" then
                count = count + 1
                write("## " .. inline(item.chapter and item.chapter ~= "" and item.chapter or ("Note " .. count)) .. "\n\n")
                local page = item.pageref or item.pageno or (type(item.page) == "number" and item.page)
                if page then write("Page " .. inline(tostring(page)) .. "\n\n") end
                if quote ~= "" then write("> " .. escape(quote):gsub("\n", "\n> ") .. "\n\n") end
                if note ~= "" then write("**Note:** " .. escape(note):gsub("\n", "  \n") .. "\n\n") end
                if item.datetime then write(inline(item.datetime) .. "\n\n") end
                write("---\n\n")
            end
        end
        if count == 0 then write("No highlights or notes in this book.\n") end
    end)
    local closed, close_error = file:close()
    if not ok or not closed then
        os.remove(output)
        return false, tostring(reason or close_error)
    end
    return true, count
end
return Notes
