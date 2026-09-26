-- Run inside a KOReader-compatible LuaJIT environment; test_epub_native.py supplies fixtures.
local script_dir = debug.getinfo(1).source:match('^@(.*/)') or './'
package.path = script_dir .. '../koreader-plugin/buddysync.koplugin/?.lua;' .. package.path
local root = assert(arg[1], 'fixture folder required')
local Archiver = require('ffi/archiver')
local lfs = require('libs/libkoreader-lfs')
package.loaded.logger = package.loaded.logger or {dbg=function() end, warn=function() end}
-- No system commands may be used anywhere in the editor/optimizer.
os.execute = function() error('Shell execution is forbidden in the optimizer') end
io.popen = function() error('Shell pipes are forbidden in the optimizer') end
local function read(path) local f=assert(io.open(path,'rb'));local s=f:read('*a');f:close();return s end
local function clean()
    for name in lfs.dir(root) do assert(not name:match('^%.haakanpoint%-work%-'), 'Leaked work file: '..name) end
end
local decodes = 0
local decode_failure, encode_failure
local encode_mode = 'legacy'
package.loaded['ui/renderimage'] = {renderImageFile=function(_, path, _, width, height)
    if decode_failure then return nil end
    decodes = decodes + 1
    assert(width <= 480 and height <= 800)
    if path:match('%.jpg$') then assert(width == 480 and height == 180, 'aspect ratio was not preserved') end
    return {getWidth=function() return width end, getHeight=function() return height end, free=function() end}
end}
package.loaded['ffi/blitbuffer'] = {TYPE_BB8=1, new=function(w,h,t)
    assert(t==1)
    return {blitFrom=function() end, free=function() end, writePNG=function(_, path)
        if encode_failure then return false,'PNG disk full' end
        if encode_mode == 'exception' then error('PNG encoder exception') end
        if encode_mode == 'nil error' then return nil, 'PNG write failed' end
        if encode_mode == 'missing' then return end
        local data = read(root..'/replacement.png')
        if encode_mode == 'empty' then data = '' end
        if encode_mode == 'truncated' then data = data:sub(1, -13) end
        if encode_mode == 'invalid' then data = 'invalid!' .. data:sub(9) end
        if encode_mode == 'wrong dimensions' then data = data:sub(1, 19) .. '\1' .. data:sub(21) end
        local f=assert(io.open(path,'wb'));assert(f:write(data));assert(f:close())
        -- Before KOReader's September 2026 change, writePNG returned no value.
        if encode_mode == 'modern' then return true end
    end}
end}
local Optimizer=require('epub_optimizer')
local Editor=require('epub_editor')
local function optimize(input, output, callback)
    local ok, result = Optimizer.optimize(root..'/'..input, output and root..'/'..output, callback)
    clean();return ok,result
end
local source=read(root..'/book.epub')
local ok_default, default_path=optimize('book.epub')
assert(ok_default,default_path)
assert(lfs.attributes(default_path,'size')>0)
assert(os.remove(default_path))
decodes=0
local ok,result=optimize('book.epub','result.epub')
assert(ok,result);assert(decodes==2,decodes);assert(read(root..'/book.epub')==source)
local size=lfs.attributes(result,'size');assert(size>0)
encode_mode='modern'
ok,result=optimize('book.epub','modern.epub');assert(ok,result)
assert(os.remove(result))
encode_mode='legacy'
-- Exercise the public editing API without image conversion.
local editor=Editor.open(root..'/book.epub')
assert(editor.files['OPS/a/cover.jpg'] and editor.files['OPS/b/cover.jpg'])
editor:write('OPS/Text/chapter.xhtml',editor:read('OPS/Text/chapter.xhtml'))
editor:save(root..'/editor-copy.epub');editor:close();clean()

for _,input in ipairs({'traversal.epub','symlink.epub','duplicate.epub','missing.epub','badspine.epub','broken.epub'}) do
    ok,result=optimize(input,'rejected.epub')
    assert(not ok,'Unsafe/broken book was accepted: '..input)
    assert(not lfs.attributes(root..'/rejected.epub'))
end
assert(not lfs.attributes(root..'/outside.txt'))
-- A runtime exception and every archive failure must clean work folders and preserve the source.
ok,result=optimize('book.epub','failed.epub',function(status)
    if status=='Stripping embedded fonts...' then error('injected processing error') end
end)
assert(not ok and result:find('injected processing error',1,true),result)
decode_failure=true;ok,result=optimize('book.epub','failed.epub');decode_failure=false
assert(not ok and result:find('decoder',1,true),result)
encode_failure=true;ok,result=optimize('book.epub','failed.epub');encode_failure=false
assert(not ok and result:find('PNG disk full',1,true),result)
for _, case in ipairs({
    {'missing', 'no output'}, {'empty', 'no output'},
    {'truncated', 'invalid or incomplete'}, {'invalid', 'invalid or incomplete'},
    {'wrong dimensions', 'invalid or incomplete'},
    {'exception', 'PNG encoder exception'}, {'nil error', 'PNG write failed'},
}) do
    encode_mode=case[1]
    ok,result=optimize('book.epub','failed.epub')
    assert(not ok and result:find(case[2],1,true),tostring(result))
    assert(not lfs.attributes(root..'/failed.epub'))
    assert(read(root..'/book.epub')==source)
end
encode_mode='legacy'
print('PASS: legacy nil and modern true PNG success returns; missing, empty, truncated and invalid images are rejected')
local extract=Archiver.Reader.extractToPath
Archiver.Reader.extractToPath=function(self) self.err='injected no space left';return false end
ok,result=optimize('book.epub','failed.epub');Archiver.Reader.extractToPath=extract
assert(not ok and result:find('no space left',1,true),result)
local add=Archiver.Writer.addFileFromMemory
Archiver.Writer.addFileFromMemory=function(self,path,...)
    if path~='mimetype' then self.err='injected archive write failure';return false end
    return add(self,path,...)
end
ok,result=optimize('book.epub','failed.epub');Archiver.Writer.addFileFromMemory=add
assert(not ok and result:find('archive write failure',1,true),result)
assert(not lfs.attributes(root..'/failed.epub'))
local finish=Archiver.Writer.close
Archiver.Writer.close=function(self)
    local path=self.filepath
    finish(self)
    -- Simulate a buffered close failure leaving an incomplete archive.
    if path then local f=assert(io.open(path,'wb'));f:write('partial archive');f:close() end
end
ok,result=optimize('book.epub','failed.epub');Archiver.Writer.close=finish
assert(not ok and result:find('not finalized',1,true),result)
assert(not lfs.attributes(root..'/failed.epub'))
-- Existing output and original EPUB must never be overwritten.
ok,result=optimize('book.epub','result.epub');assert(not ok and lfs.attributes(root..'/result.epub','size')==size)
ok,result=optimize('book.epub','book.epub');assert(not ok and read(root..'/book.epub')==source)
-- Missing native dependencies fail clearly; they never fall back to commands.
local old=package.loaded['libs/libkoreader-lfs']
package.loaded['libs/libkoreader-lfs']=nil
package.preload['libs/libkoreader-lfs']=function() error('native filesystem unavailable') end
ok,result=optimize('book.epub','failed.epub')
assert(not ok and result:find('native filesystem unavailable',1,true),result)
package.loaded['libs/libkoreader-lfs']=old;package.preload['libs/libkoreader-lfs']=nil
print('PASS: native extraction/edit/save with shell disabled; failures clean up, block output, and preserve originals')
