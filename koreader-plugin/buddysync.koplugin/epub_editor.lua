-- EPUB container editing using KOReader's bundled archive and filesystem APIs.
local XML = require('epub_xml')
local Editor = {}
Editor.__index = Editor
local sequence = 0
local function check(ok, message) if not ok then error(message, 0) end; return ok end
local function dirname(path) return path:match('^(.*)/[^/]*$') or '' end
local function normalize(path)
    local parts = {}
    for part in path:gmatch('[^/]+') do
        if part == '..' then
            check(#parts > 0, 'EPUB path escapes its container: ' .. path)
            table.remove(parts)
        elseif part ~= '.' then parts[#parts + 1] = part end
    end
    return table.concat(parts, '/')
end
local function safeEntry(path)
    check(type(path) == 'string' and path ~= '' and not path:find('[%z\\]') and
        path:sub(1, 1) ~= '/' and not path:match('^%a:'), 'Unsafe EPUB entry path')
    for p in path:gmatch('[^/]+') do check(p ~= '..', 'Unsafe EPUB entry: ' .. path) end
    local normalized = normalize(path)
    check(normalized ~= '', 'Empty EPUB entry path')
    return normalized
end
local function resolve(owner, uri)
    if uri == '' or uri:sub(1, 1) == '#' or uri:match('^[%a][%w+%.%-]*:') or uri:sub(1, 2) == '//' then return nil end
    local path, suffix = uri:match('^([^?#]*)(.*)$')
    path = path:gsub('%%(%x%x)', function(h) return string.char(tonumber(h, 16)) end)
    check(not path:find('[%z\\]') and path:sub(1, 1) ~= '/', 'Unsupported EPUB resource URL: ' .. uri)
    local base = dirname(owner)
    return normalize((base ~= '' and base .. '/' or '') .. path), suffix
end
local function relative(owner, target)
    local from, to = {}, {}
    for p in dirname(owner):gmatch('[^/]+') do from[#from + 1] = p end
    for p in target:gmatch('[^/]+') do to[#to + 1] = p end
    local common = 0
    while from[common + 1] and from[common + 1] == to[common + 1] do common = common + 1 end
    local result = {}
    for i = common + 1, #from do result[#result + 1] = '..' end
    for i = common + 1, #to do result[#result + 1] = to[i] end
    return (table.concat(result, '/'):gsub('([^%w%-%._~/])', function(c) return string.format('%%%02X', c:byte()) end))
end
local function rewriteURL(owner, value, renames)
    local path, suffix = resolve(owner, value)
    if path and renames[path] then return relative(owner, renames[path].path) .. suffix end
    return value
end
local function skipString(text, pos)
    local q = text:sub(pos, pos)
    pos = pos + 1
    while pos <= #text do
        local c = text:sub(pos, pos)
        if c == '\\' then pos = pos + 2
        elseif c == q then return pos + 1 else pos = pos + 1 end
    end
    error('Unclosed CSS string', 0)
end
local function cssURLs(text, callback)
    local patches, i = {}, 1
    while i <= #text do
        local c = text:sub(i, i)
        if text:sub(i, i + 1) == '/*' then i = assert(text:find('*/', i + 2, true), 'Unclosed CSS comment') + 2
        elseif c == '"' or c == "'" then i = skipString(text, i)
        else
            local a, b = text:find('^[uU][rR][lL]%s*%(', i)
            if a and (i == 1 or not text:sub(i - 1, i - 1):match('[%w_%-]')) then
                local first = b + 1
                while text:sub(first, first):match('%s') do first = first + 1 end
                local q = text:sub(first, first)
                local last, stop
                if q == '"' or q == "'" then
                    stop = skipString(text, first); last = stop - 2; first = first + 1
                    while text:sub(stop, stop):match('%s') do stop = stop + 1 end
                    check(text:sub(stop, stop) == ')', 'Malformed CSS URL')
                else
                    stop = assert(text:find(')', first, true), 'Unclosed CSS URL')
                    last = stop - 1
                    while last >= first and text:sub(last, last):match('%s') do last = last - 1 end
                end
                local value = text:sub(first, last)
                -- Escaped CSS URLs require a full CSS escape decoder; leave unrelated ones untouched.
                check(not value:find('\\', 1, true), 'Unsupported escaped CSS URL: ' .. value)
                local updated = callback(value)
                if updated ~= value then patches[#patches + 1] = {first, last, updated} end
                i = stop + 1
            else i = i + 1 end
        end
    end
    return XML.apply(text, patches)
end
local function rewriteCSS(text, owner, renames, removed)
    -- Remove only font-face blocks referring to fonts removed from this book.
    local patches, i = {}, 1
    while i <= #text do
        local c = text:sub(i, i)
        if text:sub(i, i + 1) == '/*' then i = assert(text:find('*/', i + 2, true)) + 2
        elseif c == '"' or c == "'" then i = skipString(text, i)
        elseif text:sub(i):match('^@[fF][oO][nN][tT]%-[fF][aA][cC][eE]%s*{') then
            local first = i
            i = assert(text:find('{', i, true)) + 1
            local depth = 1
            while depth > 0 and i <= #text do
                c = text:sub(i, i)
                if text:sub(i, i + 1) == '/*' then i = assert(text:find('*/', i + 2, true)) + 2
                elseif c == '"' or c == "'" then i = skipString(text, i)
                else
                    if c == '{' then depth = depth + 1 elseif c == '}' then depth = depth - 1 end
                    i = i + 1
                end
            end
            check(depth == 0, 'Unclosed CSS font-face rule')
            local discard = false
            cssURLs(text:sub(first, i - 1), function(url)
                local path = resolve(owner, url)
                if path and removed[path] then discard = true end
                return url
            end)
            if discard then patches[#patches + 1] = {first, i - 1, ''} end
        else i = i + 1 end
    end
    text = XML.apply(text, patches)
    return cssURLs(text, function(url) return rewriteURL(owner, url, renames) end)
end
local function deleteTree(lfs, path)
    local mode = lfs.symlinkattributes(path, 'mode')
    if not mode then return end
    if mode == 'directory' then
        for name in lfs.dir(path) do
            if name ~= '.' and name ~= '..' then deleteTree(lfs, path .. '/' .. name) end
        end
        local ok, err = lfs.rmdir(path); check(ok, 'Cannot clean work folder: ' .. tostring(err))
    else
        local ok, err = os.remove(path); check(ok, 'Cannot clean temporary file: ' .. tostring(err))
    end
end
function Editor:close()
    if self.work_dir then deleteTree(self.lfs, self.work_dir); self.work_dir = nil end
end
function Editor:absolute(path)
    if path:sub(1, 1) ~= '/' then path = assert(self.lfs.currentdir()) .. '/' .. path end
    return '/' .. normalize(path)
end
function Editor:path(path) return self.work_dir .. '/' .. safeEntry(path) end
function Editor:list()
    local files = {}; for path in pairs(self.files) do files[#files + 1] = path end
    table.sort(files); return files
end
function Editor:read(path)
    check(self.files[path], 'Missing EPUB resource: ' .. path)
    local f, err = io.open(self:path(path), 'rb'); check(f, 'Cannot read ' .. path .. ': ' .. tostring(err))
    local content, read_err = f:read('*a'); f:close()
    return check(content, 'Cannot read ' .. path .. ': ' .. tostring(read_err))
end
function Editor:write(path, content)
    local f, err = io.open(self:path(path), 'wb'); check(f, 'Cannot write ' .. path .. ': ' .. tostring(err))
    local ok, write_err = f:write(content); local closed, close_err = f:close()
    check(ok and closed, 'Cannot write ' .. path .. ': ' .. tostring(write_err or close_err))
    self.files[path] = true
end
function Editor:remove(path)
    if self.files[path] then
        local ok, err = os.remove(self:path(path)); check(ok, 'Cannot remove ' .. path .. ': ' .. tostring(err))
        self.files[path] = nil
    end
end
function Editor:replace(path, target, temporary)
    check(self.files[path], 'Missing resource to replace: ' .. path)
    check(target == path or not self.files[target], 'Replacement would overwrite another resource: ' .. target)
    local moved, err = os.rename(temporary, self:path(target))
    check(moved, 'Cannot replace ' .. path .. ': ' .. tostring(err))
    self.files[target] = true
    if target ~= path then self:remove(path) end
end
function Editor:validate()
    check(self:read('mimetype') == 'application/epub+zip', 'Missing or invalid EPUB mimetype')
    local packages = {}
    for _, node in ipairs(XML.parse(self:read('META-INF/container.xml'))) do
        if node.localname == 'rootfile' then
            local path = safeEntry(check(XML.attr(node, 'full-path'), 'Missing EPUB package path'))
            check(self.files[path], 'Missing EPUB package: ' .. path); packages[#packages + 1] = path
        end
    end
    check(#packages > 0, 'EPUB container has no package document')
    for _, path in ipairs(packages) do
        local nodes, ids = XML.parse(self:read(path)), {}
        for _, node in ipairs(nodes) do
            if node.localname == 'item' and node.parent and node.parent.localname == 'manifest' then
                local id, href = XML.attr(node, 'id'), XML.attr(node, 'href')
                check(id and href, 'Invalid manifest item in ' .. path)
                check(not ids[id], 'Duplicate manifest id: ' .. id); ids[id] = true
                local resource = resolve(path, href)
                check(not resource or self.files[resource], 'Missing manifest resource: ' .. tostring(resource))
            end
        end
        for _, node in ipairs(nodes) do
            if node.localname == 'itemref' then check(ids[XML.attr(node, 'idref')], 'Broken EPUB spine in ' .. path) end
        end
    end
    self.packages = packages
end
function Editor.open(input_path)
    local self = setmetatable({lfs = require('libs/libkoreader-lfs'), Archiver = require('ffi/archiver'), files = {}}, Editor)
    self.input_path = self:absolute(input_path)
    check(self.lfs.attributes(self.input_path, 'mode') == 'file', 'Cannot read EPUB: ' .. self.input_path)
    local input = check(io.open(self.input_path, 'rb'), 'Cannot open EPUB')
    local signature = input:read(4); input:close()
    check(signature == 'PK\003\004', 'EPUB is not a ZIP archive')
    for attempt = 1, 20 do
        sequence = sequence + 1
        local dir = dirname(self.input_path) .. '/.haakanpoint-work-' .. os.time() .. '-' .. sequence
        if not self.lfs.symlinkattributes(dir) then
            local ok, err = self.lfs.mkdir(dir)
            check(ok, 'Cannot create work folder beside book: ' .. tostring(err)); self.work_dir = dir; break
        end
    end
    check(self.work_dir, 'Could not create a unique EPUB work folder')
    local reader = self.Archiver.Reader:new()
    local ok, err = pcall(function()
        check(reader:open(self.input_path), 'Cannot open EPUB: ' .. tostring(reader.err))
        local seen = {}
        while true do
            local entry = reader:next()
            if not entry then check(not reader.err, 'Cannot read EPUB archive: ' .. tostring(reader.err)); break end
            local path = safeEntry(entry.path)
            check(not seen[path], 'Duplicate EPUB entry: ' .. path); seen[path] = true
            check(entry.mode == 'file' or entry.mode == 'directory', 'Unsupported EPUB entry type: ' .. path)
            local dirs = entry.mode == 'directory' and path or dirname(path)
            local parent = self.work_dir
            for component in dirs:gmatch('[^/]+') do
                parent = parent .. '/' .. component
                local mode = self.lfs.symlinkattributes(parent, 'mode')
                check(not mode or mode == 'directory', 'Invalid EPUB directory: ' .. path)
                if not mode then local made, why = self.lfs.mkdir(parent); check(made, 'Cannot extract EPUB: ' .. tostring(why)) end
            end
            if entry.mode == 'file' then
                check(not self.lfs.symlinkattributes(self:path(path)), 'Colliding EPUB entry: ' .. path)
                local extracted = reader:extractToPath(entry.index, self:path(path))
                check(extracted, 'Cannot extract ' .. path .. ': ' .. tostring(reader.err))
                check(self.lfs.attributes(self:path(path), 'size') == tonumber(entry.size), 'Incomplete EPUB entry: ' .. path)
                self.files[path] = true
            end
        end
        self:validate()
    end)
    local closed, close_err = pcall(function() reader:close() end)
    if not ok or not closed then
        pcall(function() self:close() end)
        error(tostring(not ok and err or close_err), 0)
    end
    return self
end
function Editor:fontResources()
    local fonts = {}
    for path in pairs(self.files) do
        local ext = path:match('%.([^%.]+)$')
        if ext and ({ttf=true,otf=true,woff=true,woff2=true,eot=true})[ext:lower()] then fonts[path] = true end
    end
    return fonts
end
function Editor:imagePath(path)
    local stem = path:gsub('%.[^%.]+$', '')
    local target, suffix = stem .. '.png', 0
    while target ~= path and self.files[target] do suffix = suffix + 1; target = stem .. '-x3-' .. suffix .. '.png' end
    return target
end
function Editor:updateResources(renames, removed)
    for _, path in ipairs(self:list()) do
        local ext = (path:match('%.([^%.]+)$') or ''):lower()
        if ext == 'css' then
            self:write(path, rewriteCSS(self:read(path), path, renames, removed))
        elseif ext == 'opf' or ext == 'xhtml' or ext == 'html' or ext == 'htm' or ext == 'svg' or ext == 'ncx' or ext == 'xml' or ext == 'smil' then
            local text = self:read(path)
            local nodes, patches, discarded = XML.parse(text), {}, {}
            for _, node in ipairs(nodes) do
                if node.localname == 'item' and node.parent and node.parent.localname == 'manifest' then
                    local href = XML.attr(node, 'href')
                    local resource = href and resolve(path, href)
                    if resource and removed[resource] then discarded[node] = true end
                elseif path == 'META-INF/encryption.xml' and node.localname == 'CipherReference' then
                    local uri = XML.attr(node, 'URI')
                    local resource = uri and resolve('', uri)
                    if resource and removed[resource] then
                        local parent = node.parent
                        while parent and parent.localname ~= 'EncryptedData' do parent = parent.parent end
                        if parent then discarded[parent] = true end
                    end
                end
            end
            for _, node in ipairs(nodes) do
                local parent, hidden = node.parent, discarded[node]
                while parent do hidden = hidden or discarded[parent]; parent = parent.parent end
                if discarded[node] then patches[#patches + 1] = {node.start, node.finish, ''}
                elseif not hidden then
                    check(not XML.attr(node, 'xml:base'), 'xml:base is not supported when optimizing ' .. path)
                    for name, attr in pairs(node.attrs) do
                        local updated = attr.value
                        if name == 'href' or name == 'src' or name == 'xlink:href' or name == 'poster' or name == 'data' then
                            updated = rewriteURL(path, attr.value, renames)
                        elseif name == 'srcset' then
                            updated = attr.value:gsub('([^,%s]+)([^,]*)', function(url, descriptor) return rewriteURL(path, url, renames) .. descriptor end)
                        elseif name == 'style' then updated = rewriteCSS(attr.value, path, renames, removed) end
                        if updated ~= attr.value then XML.set(node, name, updated, patches, text) end
                    end
                    if node.localname == 'item' and node.parent and node.parent.localname == 'manifest' then
                        local href = XML.attr(node, 'href')
                        local resource = href and resolve(path, href)
                        if resource and renames[resource] then XML.set(node, 'media-type', renames[resource].media_type, patches, text) end
                    elseif node.localname == 'style' and node.finish > node.tag_end then
                        local finish = text:find('</', node.tag_end + 1, true)
                        local css = text:sub(node.tag_end + 1, finish - 1)
                        -- Preserve CDATA wrappers around embedded CSS.
                        local lead, body, tail = css:match('^(%s*<!%[CDATA%[)(.*)(%]%]>%s*)$')
                        local updated = rewriteCSS(body or css, path, renames, removed)
                        if lead then updated = lead .. updated .. tail end
                        if updated ~= css then patches[#patches + 1] = {node.tag_end + 1, finish - 1, updated} end
                    end
                end
            end
            if #patches > 0 then self:write(path, XML.apply(text, patches)) end
        end
    end
    for path in pairs(removed) do self:remove(path) end
    self:validate()
end
function Editor:save(output_path)
    output_path = self:absolute(output_path or (self.work_dir:gsub('/%.haakanpoint%-work%-(%d+%-%d+)$', '/.haakanpoint-optimized-%1') .. '.epub'))
    check(output_path ~= self.input_path and not self.lfs.symlinkattributes(output_path), 'Output EPUB already exists: ' .. output_path)
    self:validate()
    local staged = self.work_dir .. '/.haakanpoint-output'
    check(not self.files['.haakanpoint-output'], 'Reserved EPUB entry name')
    local writer = self.Archiver.Writer:new()
    local function written(result, action) check(result, action .. ': ' .. tostring(writer.err)) end
    local ok, err = pcall(function()
        written(writer:open(staged, 'epub'), 'Cannot create EPUB')
        written(writer:setZipCompression('store'), 'Cannot store mimetype')
        written(writer:addFileFromMemory('mimetype', self:read('mimetype')), 'Cannot write mimetype')
        local compression = 'store'
        for _, path in ipairs(self:list()) do
            if path ~= 'mimetype' then
                -- These formats already compress their pixels. Storing them avoids
                -- a second inflater and its 32 KB window during X3 image extraction.
                local ext = (path:match('%.([^%.]+)$') or ''):lower()
                local method = ({png=true, jpg=true, jpeg=true, gif=true, webp=true})[ext] and 'store' or 'deflate'
                if method ~= compression then
                    written(writer:setZipCompression(method), 'Cannot set EPUB compression')
                    compression = method
                end
                written(writer:addFileFromMemory(path, self:read(path)), 'Cannot write ' .. path)
            end
        end
    end)
    local closed, close_err = pcall(function() writer:close() end)
    check(ok and closed, tostring(not ok and err or close_err))
    -- The archive wrapper does not expose close errors. Check its final directory record.
    local f = check(io.open(staged, 'rb'), 'Cannot read completed EPUB')
    local size = f:seek('end')
    local tail
    if size and size >= 22 then f:seek('end', -22); tail = f:read(22) end
    f:close()
    check(tail and tail:sub(1, 4) == 'PK\005\006', 'EPUB archive was not finalized; check free storage')
    local moved, why = os.rename(staged, output_path); check(moved, 'Cannot save EPUB: ' .. tostring(why))
    return output_path, size
end
return Editor
