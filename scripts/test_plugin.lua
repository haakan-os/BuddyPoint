-- Run with: lua scripts/test_plugin.lua (or luajit).
-- KOReader UI, optimizer, and upload client are stubbed to verify transfer gating.
local script_dir = debug.getinfo(1).source:match("^@(.*/)") or "./"
local plugin_dir = script_dir .. "../koreader-plugin/buddysync.koplugin/"
local messages, uploads, pending = {}, {}, {}
local noop = function() end
local widgets = { new = function(_, options) return options end }
local optimizer_stub = {}
package.loaded["logger"] = { dbg = noop, warn = noop }
package.loaded["gettext"] = function(text) return text end
package.loaded["ui/widget/container/widgetcontainer"] = {
    extend = function(_, definition) return definition end,
}
package.loaded["ui/uimanager"] = {
    show = function(_, widget) messages[#messages + 1] = widget.text end,
    close = noop,
    scheduleIn = function(_, _, callback) pending[#pending + 1] = callback end,
}
for _, name in ipairs({"ui/widget/infomessage", "ui/widget/inputdialog", "ui/widget/notification"}) do
    package.loaded[name] = widgets
end
for _, name in ipairs({"luasettings", "datastorage", "readhistory", "docsettings",
    "socket.http", "ltn12", "json", "mime"}) do
    package.loaded[name] = {}
end
package.loaded["epub_optimizer"] = optimizer_stub
local plugin = dofile(plugin_dir .. "main.lua")
-- Upgrading from HaakanPoint must preserve the existing settings without
-- requiring credentials or the device address to be entered again.
local saved_settings = {
    x3_host = "192.0.2.42", kosync_server = "https://example.invalid",
    username = "test-user", password_md5 = "test-hash",
    optimize_epub = false, auto_push_book = false, auto_sync_progress = false,
    shelf_sync_limit = 3,
}
local saved_copy = {}
for key, value in pairs(saved_settings) do saved_copy[key] = value end
package.loaded["datastorage"].getSettingsDir = function() return "/settings" end
local db = {
    readSetting = function(_, key) assert(key == "settings"); return saved_settings end,
    saveSetting = function(_, key, value) assert(key == "settings" and value == saved_settings) end,
    flush = noop,
}
package.loaded["luasettings"].open = function(_, path)
    assert(path == "/settings/haakanpoint.lua", "existing settings must remain accessible")
    return db
end
local registered = false
plugin.ui = { menu = { registerToMainMenu = function(_, item)
    assert(item == plugin); registered = true
end } }
plugin:init()
assert(registered and plugin.name == "buddysync")
for key, value in pairs(saved_copy) do assert(plugin.settings[key] == value, key) end
assert(plugin.client.x3_host == saved_copy.x3_host and plugin.client.username == saved_copy.username)
plugin:saveSettings()
local menu = {}
plugin:addToMainMenu(menu)
assert(menu.buddysync.text == "BuddySync" and menu.haakanpoint == nil)
assert(dofile(plugin_dir .. "_meta.lua").fullname == "BuddySync")
plugin.settings_db = nil
print("PASS: BuddySync branding and existing settings survive the rename")

local active = {{path = "/books/first.epub"}, {path = "/books/second.epub"}}
plugin.ui = { document = { file = active[1].path } }
plugin.settings = { optimize_epub = true, shelf_sync_limit = 5 }
plugin.getCurrentlyReadingBooks = function() return active end
plugin.client = { uploadBookToX3 = function(_, path)
    uploads[#uploads + 1] = path
    return true
end }

local function run(action, optimize)
    messages, uploads, pending = {}, {}, {}
    optimizer_stub.optimize = optimize
    plugin[action](plugin)
    for _, callback in ipairs(pending) do callback() end
end
local function lastMessageContains(text)
    assert(messages[#messages]:find(text, 1, true), messages[#messages])
end

for _, action in ipairs({"sendCurrentBook", "syncActiveShelf"}) do
    run(action, function() return false, "Failed to repack: zip unavailable" end)
    assert(#uploads == 0, action .. " uploaded after optimization failure")
    lastMessageContains("zip unavailable")
    lastMessageContains("Book not sent")

    run(action, function() error("image decoder crashed") end)
    assert(#uploads == 0, action .. " uploaded after an exception")
    lastMessageContains("image decoder crashed")
end

-- Use actual temporary files so successful cleanup cannot touch a user's books.
local optimized = os.tmpname()
run("sendCurrentBook", function() return true, optimized, 1000, 500 end)
assert(#uploads == 1 and uploads[1] == optimized)
lastMessageContains("Optimized book transferred")
assert(io.open(optimized, "rb") == nil, "temporary EPUB was not cleaned up")

optimized = os.tmpname()
run("syncActiveShelf", function(path)
    if path == active[1].path then return true, optimized end
    return false, "second book cannot be optimized"
end)
assert(#uploads == 1 and uploads[1] == optimized)
lastMessageContains("second book cannot be optimized")
lastMessageContains("Transferred 1 of 2")
assert(io.open(optimized, "rb") == nil)

plugin.settings.optimize_epub = false
run("sendCurrentBook", function() error("must not optimize when disabled") end)
assert(#uploads == 1 and uploads[1] == active[1].path)
plugin.settings.optimize_epub = true
plugin.ui.document.file = "/books/notes.txt"
run("sendCurrentBook", function() error("must not optimize text files") end)
assert(#uploads == 1 and uploads[1] == "/books/notes.txt")
print("PASS: single-book and shelf transfers stop on failures/exceptions; successful transfers still work")

-- Exercise both actual upload implementations with a consuming HTTP client.
plugin:saveSettings()
package.loaded["socket"] = {}
local clients = {
    plugin.client,
    dofile(plugin_dir .. "sync_client.lua"):new(),
}
local http = package.loaded["socket.http"]
package.loaded["ltn12"].sink = { table = function(target)
    return function(chunk)
        if chunk then target[#target + 1] = chunk end
        return 1
    end
end }
local original_open = io.open
local path = os.tmpname()
for _, client in ipairs(clients) do
    for _, scenario in ipairs({"empty", "small", "multiple chunks", "timeout", "exception", "server error", "read error"}) do
        local contents = scenario == "empty" and "" or
            (scenario == "multiple chunks" and string.rep("epub\0", 20000) or "book contents")
        local fixture = assert(original_open(path, "wb"))
        assert(fixture:write(contents))
        fixture:close()
        local closes, reads, handle, source = 0, 0
        io.open = function(name, mode)
            assert(name == path and mode == "rb")
            handle = assert(original_open(name, mode))
            return {
                seek = function(_, ...) return handle:seek(...) end,
                read = function(_, ...)
                    reads = reads + 1
                    if scenario == "read error" then return nil, "simulated read failure" end
                    return handle:read(...)
                end,
                close = function()
                    closes = closes + 1
                    return handle:close()
                end,
            }
        end
        http.request = function(request)
            source = request.source
            if scenario == "timeout" or scenario == "exception" then
                assert(source()) -- Failure before the file is fully consumed.
                if scenario == "exception" then error("connection interrupted") end
                return nil, "timeout"
            end
            local chunks = {}
            while true do
                local chunk, err = source()
                if err then return nil, err end
                if not chunk then break end
                chunks[#chunks + 1] = chunk
            end
            local reads_at_eof = reads
            assert(source() == nil and source() == nil)
            assert(reads == reads_at_eof, "EOF must not read the file again")
            local boundary = request.headers["Content-Type"]:match("boundary=(.+)$")
            local expected = "--" .. boundary .. "\r\n" ..
                'Content-Disposition: form-data; name="file"; filename="test.epub"\r\n' ..
                "Content-Type: application/epub+zip\r\n\r\n" .. contents ..
                "\r\n--" .. boundary .. "--\r\n"
            assert(table.concat(chunks) == expected, "multipart body changed")
            assert(tonumber(request.headers["Content-Length"]) == #expected)
            if scenario == "server error" then
                request.sink("device storage full")
                return 1, 500
            end
            return 1, 200
        end
        local ok, detail = client:uploadBookToX3(path, "test.epub")
        io.open = original_open
        assert(closes == 1 and io.type(handle) == "closed file", "upload must close the file exactly once")
        local expected_error = ({timeout = "timeout", exception = "connection interrupted",
            ["server error"] = "device storage full", ["read error"] = "simulated read failure"})[scenario]
        if expected_error then
            assert(not ok and detail:find(expected_error, 1, true), detail)
        else
            assert(ok, detail)
            -- The source remains at EOF even after the request closes its file.
            assert(source() == nil)
        end
    end
end
os.remove(path)
print("PASS: both upload clients stream complete multipart bodies, remain at EOF, and close files on success/failure")
