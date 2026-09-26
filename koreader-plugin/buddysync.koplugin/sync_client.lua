local http = require("socket.http")
local ltn12 = require("ltn12")
local json = require("json")
local socket = require("socket")
local mime = require("mime")
local logger = require("logger")

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

-- Check connectivity to Xteink X3
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

-- Upload an EPUB file directly to Xteink X3 via HTTP multipart
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

-- Pull reading position from KOSync server
function SyncClient:pullProgress(document_hash)
    if not document_hash or document_hash == "" then return nil end
    local url = self.kosync_server .. "/users/progress/" .. document_hash
    local response_body = {}
    http.TIMEOUT = self.timeout
    
    local auth_header = ""
    if self.username and self.username ~= "" then
        auth_header = "Basic " .. mime.b64(self.username .. ":" .. self.password_md5)
    end
    
    local ok, status_code = pcall(function()
        local _, code = http.request{
            url = url,
            method = "GET",
            headers = {
                ["Accept"] = "application/vnd.koreader.v1+json",
                ["Authorization"] = auth_header,
            },
            sink = ltn12.sink.table(response_body),
        }
        return code
    end)
    
    if ok and status_code == 200 then
        local raw = table.concat(response_body)
        local parse_ok, data = pcall(json.decode, raw)
        if parse_ok then return data end
    end
    return nil
end

-- Push reading position to KOSync server
function SyncClient:pushProgress(document_hash, progress_pct, spine_index, xpath)
    if not document_hash or document_hash == "" then return false end
    local url = self.kosync_server .. "/users/progress"
    
    local payload = {
        document = document_hash,
        progress = progress_pct / 100.0,
        percentage = progress_pct,
        timestamp = os.time(),
        device = "koreader-kindle",
        progress_detail = {
            spine = spine_index or 0,
            xpath = xpath or "",
        }
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

return SyncClient
