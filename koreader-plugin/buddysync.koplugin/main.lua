local WidgetContainer = require("ui/widget/container/widgetcontainer")
local UIManager = require("ui/uimanager")
local InfoMessage = require("ui/widget/infomessage")
local InputDialog = require("ui/widget/inputdialog")
local Notification = require("ui/widget/notification")
local LuaSettings = require("luasettings")
local DataStorage = require("datastorage")
local logger = require("logger")
local _ = require("gettext")

local ReadHistory = require("readhistory")
local DocSettings = require("docsettings")
local http = require("socket.http")
local ltn12 = require("ltn12")
local json = require("json")
local mime = require("mime")

local ok_opt, EpubOptimizer = pcall(require, "epub_optimizer")
if not ok_opt then
    local plugin_dir = debug.getinfo(1).source:match("@?(.*/)")
    package.path = package.path .. ";" .. plugin_dir .. "?.lua"
    EpubOptimizer = require("epub_optimizer")
end

-- Embedded Sync Client
local SyncClient = {}

function SyncClient:new(x3_host, kosync_server, username, password_md5)
    local o = {
        x3_host = x3_host or "haakanpoint.local",
        kosync_server = kosync_server or "https://sync.koreader.rocks",
        username = username or "",
        password_md5 = password_md5 or "",
        timeout = 5,
    }
    setmetatable(o, { __index = self })
    return o
end

function SyncClient:pingX3()
    local url = "http://" .. self.x3_host .. "/api/status"
    local response_body = {}
    http.TIMEOUT = self.timeout
    
    local ok, status_code = pcall(function()
        local _, code = http.request{
            url = url,
            method = "GET",
            sink = ltn12.sink.table(response_body),
        }
        return code
    end)
    
    if ok and status_code == 200 then
        local raw = table.concat(response_body)
        local parse_ok, data = pcall(json.decode, raw)
        if parse_ok and data then return true, data end
    end
    return false, nil
end

function SyncClient:uploadBookToX3(filepath, filename)
    local file = io.open(filepath, "rb")
    if not file then
        return false, "Could not open local book file"
    end
    
    local filesize = file:seek("end")
    file:seek("set", 0)
    
    local clean_name = filename or "book.epub"
    local boundary = "----BuddySyncBoundary" .. tostring(os.time())
    
    local header = "--" .. boundary .. "\r\n" ..
                   'Content-Disposition: form-data; name="file"; filename="' .. clean_name .. '"\r\n' ..
                   "Content-Type: application/epub+zip\r\n\r\n"
    local footer = "\r\n--" .. boundary .. "--\r\n"
    
    local total_len = #header + filesize + #footer
    
    local CHUNK_SIZE = 32768
    local header_sent = false
    local footer_sent = false
    
    local custom_source = function()
        -- HTTP asks for EOF after consuming the multipart footer.
        if footer_sent then return nil end
        if not header_sent then
            header_sent = true
            return header
        end
        local chunk, read_error = file:read(CHUNK_SIZE)
        if chunk then
            return chunk
        end
        footer_sent = true
        if read_error then return nil, read_error end
        return footer
    end
    
    local clean_host = self.x3_host:gsub("^https?://", ""):gsub("/+$", "")
    local url = "http://" .. clean_host .. "/api/upload?overwrite=true"
    local response_body = {}
    http.TIMEOUT = 300
    
    local ok, status_code = pcall(function()
        local _, code = http.request{
            url = url,
            method = "POST",
            headers = {
                ["Content-Type"] = "multipart/form-data; boundary=" .. boundary,
                ["Content-Length"] = tostring(total_len),
            },
            source = custom_source,
            sink = ltn12.sink.table(response_body),
        }
        return code
    end)
    
    -- Own the handle here so success and early request failures close it once.
    pcall(function() file:close() end)
    
    if ok and (status_code == 200 or status_code == 302) then
        return true, "Successfully transferred to Xteink X3"
    else
        local resp = table.concat(response_body)
        local err_detail = (resp and resp ~= "") and resp or ("Status: " .. tostring(status_code))
        return false, "Upload failed (" .. err_detail .. ")"
    end
end

function SyncClient:pushProgress(document_hash, progress_pct)
    if not document_hash or document_hash == "" then return false end
    local url = self.kosync_server .. "/users/progress"
    
    local payload = {
        document = document_hash,
        progress = progress_pct / 100.0,
        percentage = progress_pct,
        timestamp = os.time(),
        device = "koreader-kindle",
    }
    
    local body = json.encode(payload)
    local response_body = {}
    http.TIMEOUT = self.timeout
    
    local auth_header = ""
    if self.username and self.username ~= "" then
        auth_header = "Basic " .. mime.b64(self.username .. ":" .. self.password_md5)
    end
    
    local ok, status_code = pcall(function()
        local _, code = http.request{
            url = url,
            method = "PUT",
            headers = {
                ["Content-Type"] = "application/json",
                ["Accept"] = "application/vnd.koreader.v1+json",
                ["Authorization"] = auth_header,
                ["Content-Length"] = tostring(#body),
            },
            source = ltn12.source.string(body),
            sink = ltn12.sink.table(response_body),
        }
        return code
    end)
    
    return ok and (status_code == 200 or status_code == 201)
end

-- Plugin Definition
local BuddySyncPlugin = WidgetContainer:extend{
    name = "buddysync",
    is_doc_only = false,
}

function BuddySyncPlugin:init()
    -- Keep the legacy settings file so existing addresses, credentials and preferences survive the rename.
    local settings_path = DataStorage:getSettingsDir() .. "/haakanpoint.lua"
    self.settings_db = LuaSettings:open(settings_path)
    self.settings = self.settings_db:readSetting("settings") or {
        x3_host = "haakanpoint.local",
        kosync_server = "https://sync.koreader.rocks",
        username = "",
        password_md5 = "",
        auto_push_book = true,
        auto_sync_progress = true,
        optimize_epub = true,
        shelf_sync_limit = 5,
    }
    if self.settings.optimize_epub == nil then
        self.settings.optimize_epub = true
    end

    self.client = SyncClient:new(
        self.settings.x3_host,
        self.settings.kosync_server,
        self.settings.username,
        self.settings.password_md5
    )

    self.ui.menu:registerToMainMenu(self)
end

function BuddySyncPlugin:saveSettings()
    if self.settings_db then
        self.settings_db:saveSetting("settings", self.settings)
        self.settings_db:flush()
    end
    self.client = SyncClient:new(
        self.settings.x3_host,
        self.settings.kosync_server,
        self.settings.username,
        self.settings.password_md5
    )
end

function BuddySyncPlugin:getCurrentlyReadingBooks(limit)
    limit = limit or self.settings.shelf_sync_limit or 5
    local active_books = {}
    local history = ReadHistory.history or {}
    
    for _, item in ipairs(history) do
        local path = item.path
        if path and (path:lower():match("%.epub$") or path:lower():match("%.txt$")) then
            local doc_settings = DocSettings:open(path)
            local status = "reading"
            local progress = 0
            
            if doc_settings and doc_settings.data then
                local summary = doc_settings.data.summary or {}
                status = summary.status or "reading"
                progress = doc_settings.data.percent_finished or 0
            end
            
            if status == "reading" or (progress > 0 and progress < 0.99) then
                local filename = path:match("([^/]+)$") or path
                table.insert(active_books, {
                    path = path,
                    title = item.text or filename,
                    progress = progress,
                    time = item.time or 0,
                })
                if #active_books >= limit then break end
            end
        end
    end
    return active_books
end

function BuddySyncPlugin:addToMainMenu(menu_items)
    menu_items.buddysync = {
        text = _("BuddySync"),
        sorting_hint = "tools",
        sub_item_table = {
            {
                text = _("Send Current Book to Xteink X3"),
                callback = function()
                    self:sendCurrentBook()
                end,
            },
            {
                text = _("Sync Active 'Currently Reading' Shelf to X3"),
                callback = function()
                    self:syncActiveShelf()
                end,
            },
            {
                text = _("Sync Reading Position Now"),
                callback = function()
                    self:syncPositionNow()
                end,
            },
            {
                text = _("Test Connection to X3"),
                callback = function()
                    self:testConnection()
                end,
            },
            {
                text = _("Optimize EPUB for Xteink X3"),
                help_text = _("Downscales images to 480x800 & strips dead-weight fonts before transfer"),
                checked_func = function() return self.settings.optimize_epub end,
                callback = function()
                    self.settings.optimize_epub = not self.settings.optimize_epub
                    self:saveSettings()
                end,
            },
            {
                text = _("Auto-Push Active Books"),
                checked_func = function() return self.settings.auto_push_book end,
                callback = function()
                    self.settings.auto_push_book = not self.settings.auto_push_book
                    self:saveSettings()
                end,
            },
            {
                text = _("Auto-Sync Progress on Open/Close"),
                checked_func = function() return self.settings.auto_sync_progress end,
                callback = function()
                    self.settings.auto_sync_progress = not self.settings.auto_sync_progress
                    self:saveSettings()
                end,
            },
            {
                text = _("Set Xteink X3 IP / Address"),
                help_text = _("Default: haakanpoint.local"),
                callback = function()
                    self:showHostDialog()
                end,
            },
        }
    }
end

function BuddySyncPlugin:sendCurrentBook()
    if not self.ui or not self.ui.document then
        UIManager:show(InfoMessage:new{ text = _("Please open a book first.") })
        return
    end
    local doc_path = self.ui.document.file
    local filename = doc_path:match("([^/]+)$") or "book.epub"

    local upload_path = doc_path
    local is_epub = doc_path:lower():match("%.epub$")
    local should_optimize = self.settings.optimize_epub and is_epub

    local client = self.client
    local notif = Notification:new{
        text = should_optimize and (_("Optimizing '") .. filename .. _("' for Xteink X3..."))
                               or (_("Sending '") .. filename .. _("' to Xteink X3...")),
    }
    UIManager:show(notif)

    UIManager:scheduleIn(0.2, function()
        local cleanup_file = nil
        if should_optimize then
            local status_cb = function(status_text)
                if notif and notif.setText then
                    pcall(function() notif:setText(status_text) end)
                    pcall(function() UIManager:setDirty(notif, "ui") end)
                end
            end

            local pcall_ok, opt_ok, opt_path, in_sz, out_sz = pcall(function()
                return EpubOptimizer.optimize(doc_path, nil, status_cb)
            end)

            if pcall_ok and opt_ok and opt_path then
                upload_path = opt_path
                cleanup_file = opt_path
                local saved_pct = (in_sz and in_sz > 0 and out_sz) and math.floor(((in_sz - out_sz) / in_sz) * 100) or 0
                local out_mb = out_sz and string.format("%.1f", out_sz / 1048576) or "?"
                local in_mb = in_sz and string.format("%.1f", in_sz / 1048576) or "?"
                logger.dbg(string.format("[BuddySync] Optimized: %s MB → %s MB (-%d%%)", in_mb, out_mb, saved_pct))
                if notif and notif.setText then
                    pcall(function() notif:setText(string.format("Optimized: %s → %s MB (-%d%%)", in_mb, out_mb, saved_pct)) end)
                    pcall(function() UIManager:setDirty(notif, "ui") end)
                end
            else
                -- Show the actual error to the user instead of silently falling back
                local err_reason
                if not pcall_ok then
                    err_reason = tostring(opt_ok)  -- pcall error message
                else
                    err_reason = tostring(opt_path or opt_ok or "unknown")  -- optimizer error
                end
                logger.warn("[BuddySync] Optimization FAILED:", err_reason)
                pcall(function() UIManager:close(notif) end)
                UIManager:show(InfoMessage:new{
                    text = _("Book not sent. Optimization failed for: ") .. filename .. "\n\n" .. err_reason,
                })
                return
            end
        end

        if notif and notif.setText then
            pcall(function() notif:setText(_("Streaming book to Xteink X3...")) end)
            pcall(function() UIManager:setDirty(notif, "ui") end)
        end

        local ok, msg = client:uploadBookToX3(upload_path, filename)
        if cleanup_file then
            pcall(function() os.remove(cleanup_file) end)
        end

        pcall(function() UIManager:close(notif) end)

        -- Show result with file size info so user can verify optimization worked
        local result_text
        if ok then
            local sent_size = 0
            pcall(function()
                -- upload_path may already be deleted (cleanup_file), use the recorded size
                if upload_path ~= doc_path then
                    -- Was optimized - show the optimized size from the log
                    result_text = _("Success! Optimized book transferred to Xteink X3.")
                else
                    result_text = _("Success! Book transferred to Xteink X3.")
                end
            end)
            result_text = result_text or _("Success! Book transferred to Xteink X3.")
        else
            result_text = _("Transfer Failed: ") .. tostring(msg)
        end
        UIManager:show(InfoMessage:new{ text = result_text })
    end)
end

function BuddySyncPlugin:syncActiveShelf()
    local active = self:getCurrentlyReadingBooks(self.settings.shelf_sync_limit)
    if #active == 0 then
        UIManager:show(InfoMessage:new{
            text = _("No active books currently marked as reading."),
        })
        return
    end

    UIManager:show(InfoMessage:new{
        text = string.format(_("Syncing %d active books to Xteink X3...\nThis may take a while."), #active),
        timeout = 2,
    })

    local client = self.client
    local should_optimize = self.settings.optimize_epub

    UIManager:scheduleIn(0.5, function()
        local success_count = 0
        for book_index, book in ipairs(active) do
            local filename = book.path:match("([^/]+)$") or "book.epub"
            local upload_path = book.path
            local cleanup_file = nil
            local is_epub = book.path:lower():match("%.epub$")

            if should_optimize and is_epub then
                local call_ok, opt_ok, opt_path = pcall(EpubOptimizer.optimize, book.path)
                if call_ok and opt_ok and opt_path then
                    upload_path = opt_path
                    cleanup_file = opt_path
                else
                    local err_reason = tostring((not call_ok and opt_ok) or opt_path or "Unknown optimization error")
                    logger.warn("[BuddySync] Shelf optimization FAILED:", filename, err_reason)
                    UIManager:show(InfoMessage:new{
                        text = _("Shelf sync stopped. Book not sent: ") .. filename .. "\n\n" .. err_reason ..
                            "\n\n" .. string.format(_("Transferred %d of %d books before stopping."), success_count, #active),
                    })
                    return
                end
            end

            local ok = client:uploadBookToX3(upload_path, filename)
            if cleanup_file then
                pcall(function() os.remove(cleanup_file) end)
            end

            if ok then success_count = success_count + 1 end
        end

        UIManager:show(InfoMessage:new{
            text = string.format(_("Shelf Sync Complete!\nSuccessfully synced %d of %d books to your Xteink X3."), success_count, #active),
        })
    end)
end

function BuddySyncPlugin:syncPositionNow()
    if not self.ui or not self.ui.document then return end
    local doc = self.ui.document
    local doc_hash = doc.checksum or ""
    local curr_page = (self.ui.state and self.ui.state.page) or 1
    local total_pages = doc.pages or 1
    local pct = (curr_page / total_pages) * 100.0

    local client = self.client
    UIManager:scheduleIn(0.1, function()
        local ok = client:pushProgress(doc_hash, pct)
        UIManager:show(Notification:new{
            text = ok and _("Reading position synced with BuddySync!") or _("Sync Failed (Check network)"),
        })
    end)
end

function BuddySyncPlugin:testConnection()
    UIManager:show(Notification:new{
        text = _("Connecting to Xteink X3..."),
    })

    local client = self.client
    local host = self.settings.x3_host
    UIManager:scheduleIn(0.5, function()
        local ok, data = client:pingX3()
        if ok and data then
            local msg = string.format("Connected to %s\nBattery: %s%%\nFree Storage: %s MB",
                tostring(data.device or "Xteink X3"),
                tostring(data.battery_pct or 100),
                tostring(data.sd_free_mb or 0))
            UIManager:show(InfoMessage:new{ text = msg })
        else
            UIManager:show(InfoMessage:new{
                text = _("Could not reach Xteink X3 at ") .. host .. _(".\nMake sure File Transfer/Wi-Fi is ON on your X3.")
            })
        end
    end)
end

function BuddySyncPlugin:showHostDialog()
    local dialog
    dialog = InputDialog:new{
        title = _("Xteink X3 Address / Hostname"),
        input = self.settings.x3_host,
        description = _("Enter the mDNS name (e.g. haakanpoint.local) or IP address of your Xteink X3:"),
        buttons = {
            {
                {
                    text = _("Cancel"),
                    id = "cancel",
                    callback = function()
                        UIManager:close(dialog)
                    end,
                },
                {
                    text = _("Save"),
                    is_enter_default = true,
                    callback = function()
                        local input = dialog:getInputText()
                        if input and input ~= "" then
                            self.settings.x3_host = input
                            self:saveSettings()
                        end
                        UIManager:close(dialog)
                    end,
                },
            },
        },
    }
    UIManager:show(dialog)
    dialog:onShowKeyboard()
end

-- Event Hook: When a book finishes opening in KOReader
function BuddySyncPlugin:onReaderReady()
    if not self.settings.auto_sync_progress then return end
    
    if self.settings.auto_push_book and self.ui and self.ui.document then
        local doc_path = self.ui.document.file
        if doc_path and (doc_path:lower():match("%.epub$") or doc_path:lower():match("%.txt$")) then
            logger.dbg("[BuddySync] Reader ready for book:", doc_path)
        end
    end
end

function BuddySyncPlugin:onCloseDocument()
    if self.settings.auto_sync_progress and self.ui and self.ui.document then
        self:syncPositionNow()
    end
end

function BuddySyncPlugin:onSuspend()
    if self.settings.auto_sync_progress and self.ui and self.ui.document then
        self:syncPositionNow()
    end
end

return BuddySyncPlugin
