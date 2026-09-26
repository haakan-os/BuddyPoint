-- Optimize an EPUB using KOReader's archive, filesystem, and image APIs only.
local Editor = require('epub_editor')
local logger = require('logger')
local Optimizer = { MAX_WIDTH = 480, MAX_HEIGHT = 800, MIN_IMAGE_SIZE = 2048 }
local function getImageDimensions(filepath)
    local f = io.open(filepath, "rb")
    if not f then return nil, nil end
    local header = f:read(32)
    f:close()

    if not header or #header < 16 then return nil, nil end

    -- PNG signature: 89 50 4E 47 0D 0A 1A 0A
    if header:sub(1, 8) == "\137PNG\r\n\26\n" and #header >= 24 then
        local w = (header:byte(17) * 16777216) + (header:byte(18) * 65536) + (header:byte(19) * 256) + header:byte(20)
        local h = (header:byte(21) * 16777216) + (header:byte(22) * 65536) + (header:byte(23) * 256) + header:byte(24)
        return w, h
    end

    -- JPEG SOI: FF D8
    if header:byte(1) == 0xFF and header:byte(2) == 0xD8 then
        local f_jpg = io.open(filepath, "rb")
        if f_jpg then
            f_jpg:seek("set", 2)
            while true do
                local seg_header = f_jpg:read(4)
                if not seg_header or #seg_header < 4 then break end
                if seg_header:byte(1) ~= 0xFF then break end
                local marker = seg_header:byte(2)
                local len = (seg_header:byte(3) * 256) + seg_header:byte(4)
                if marker >= 0xC0 and marker <= 0xCF and marker ~= 0xC4 and marker ~= 0xC8 and marker ~= 0xCC then
                    local sof = f_jpg:read(5)
                    if sof and #sof >= 5 then
                        local h = (sof:byte(2) * 256) + sof:byte(3)
                        local w = (sof:byte(4) * 256) + sof:byte(5)
                        f_jpg:close()
                        return w, h
                    end
                    break
                else
                    f_jpg:seek("cur", len - 2)
                end
            end
            f_jpg:close()
        end
    end

    return nil, nil
end

local function fit(width, height)
    local scale = math.min(1, Optimizer.MAX_WIDTH / width, Optimizer.MAX_HEIGHT / height)
    return math.max(1, math.floor(width * scale)), math.max(1, math.floor(height * scale))
end
local function optimizeImage(editor, path, RenderImage, Blitbuffer)
    local target = editor:imagePath(path)
    local temporary = editor:path(target) .. '.haakanpoint-tmp'
    if editor.lfs.symlinkattributes(temporary) then error('Temporary image path already exists: ' .. target, 0) end
    local bb, gray
    local ok, err = pcall(function()
        local width, height = getImageDimensions(editor:path(path))
        if width and height and width > 0 and height > 0 then width, height = fit(width, height) end
        bb = RenderImage:renderImageFile(editor:path(path), false, width, height)
        if not bb then error('Image decoder returned no image', 0) end
        local w, h = bb:getWidth(), bb:getHeight()
        local tw, th = fit(w, h)
        if w ~= tw or h ~= th then
            local original = bb
            bb = RenderImage:scaleBlitBuffer(original, tw, th, false)
            if bb ~= original then original:free() end
            if not bb then error('Image resize failed', 0) end
        end
        gray = Blitbuffer.new(bb:getWidth(), bb:getHeight(), Blitbuffer.TYPE_BB8)
        gray:blitFrom(bb)
        local saved, why = gray:writePNG(temporary)
        -- Older KOReader writePNG implementations return nothing, even on success.
        -- Honor explicit errors and verify the file instead of requiring true.
        if saved == false or why ~= nil then error('PNG encoding failed: ' .. tostring(why or 'unknown error'), 0) end
        local size = editor.lfs.attributes(temporary, 'size')
        if not size or size == 0 then error('PNG encoder produced no output', 0) end
        local output = io.open(temporary, 'rb')
        if not output then error('Could not read encoded PNG', 0) end
        local signature = output:read(8)
        local ending
        if size >= 45 then
            output:seek('end', -12)
            ending = output:read(12)
        end
        output:close()
        local pw, ph = getImageDimensions(temporary)
        if signature ~= '\137PNG\r\n\26\n' or ending ~= '\0\0\0\0IEND\174\66\96\130' or
            pw ~= bb:getWidth() or ph ~= bb:getHeight() then
            error('PNG encoder produced invalid or incomplete output', 0)
        end
        editor:replace(path, target, temporary)
    end)
    if gray then pcall(function() gray:free() end) end
    if bb then pcall(function() bb:free() end) end
    os.remove(temporary)
    if not ok then error("Could not optimize image '" .. path .. "': " .. tostring(err), 0) end
    return target
end

function Optimizer.optimize(input_path, output_path, status_callback)
    local editor, result_path
    local function status(text) if status_callback then status_callback(text) end end
    local ok, result, original_size, optimized_size = pcall(function()
        if type(input_path) ~= 'string' or input_path == '' then error('Invalid input EPUB', 0) end
        status('Opening EPUB with KOReader...')
        editor = Editor.open(input_path)
        local input_size = editor.lfs.attributes(editor.input_path, 'size')
        status('Stripping embedded fonts...')
        local removed = editor:fontResources()
        local renames, count = {}, 0
        local RenderImage, Blitbuffer
        for _, path in ipairs(editor:list()) do
            local ext = (path:match('%.([^%.]+)$') or ''):lower()
            if ({jpg=true, jpeg=true, png=true, webp=true, bmp=true})[ext] and
                editor.lfs.attributes(editor:path(path), 'size') >= Optimizer.MIN_IMAGE_SIZE then
                RenderImage = RenderImage or require('ui/renderimage')
                Blitbuffer = Blitbuffer or require('ffi/blitbuffer')
                count = count + 1
                status('Optimizing image ' .. count .. ' for Xteink X3...')
                local target = optimizeImage(editor, path, RenderImage, Blitbuffer)
                if target ~= path then renames[path] = {path = target, media_type = 'image/png'} end
            end
        end
        status('Updating EPUB resource references...')
        editor:updateResources(renames, removed)
        status('Rebuilding EPUB with KOReader...')
        local saved_path, size = editor:save(output_path)
        result_path = saved_path
        return saved_path, input_size, size
    end)
    local cleanup_ok, cleanup_err = true, nil
    if editor then cleanup_ok, cleanup_err = pcall(function() editor:close() end) end
    if not ok or not cleanup_ok then
        if result_path then os.remove(result_path) end
        local reason = tostring(not ok and result or cleanup_err)
        logger.warn('[BuddySync] EPUB optimization failed:', reason)
        return false, reason
    end
    return true, result, original_size, optimized_size
end
return Optimizer
