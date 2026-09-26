-- Run with lua scripts/test_notes.lua; includes existing plugin/transport regressions.
dofile("scripts/test_plugin.lua")
local Notes = require("notes_exporter")
package.loaded["ffi/sha2"] = { md5 = function(path)
    return path:find("other", 1, true) and string.rep("b", 32) or string.rep("a", 32)
end }
local filename = Notes.filename('/books/A "book".epub')
assert(not filename:find('["/\\\r\n]') and filename:match('%.md$'))
assert(filename ~= Notes.filename('/other/A "book".epub'))
local path = os.tmpname()
local function read(p) local f = assert(io.open(p, "rb")); local s = f:read("*a"); f:close(); return s end
local annotations = {
    {drawer="lighten", text="Café *quote*\nsecond line", note="A note\n- [ ] literal", chapter="A # chapter", pageno=12},
    {text="Ordinary bookmark", page=2},
    {note="A standalone note", pageno=15},
}
local ok, count = Notes.write(path, "Title\nheading", "Author", annotations)
assert(ok and count == 2)
local output = read(path)
assert(output:find('Café \\*quote\\*', 1, true))
assert(output:find('> second line', 1, true))
assert(output:find('Page 12', 1, true))
assert(output:find('A standalone note', 1, true))
assert(not output:find('Ordinary bookmark', 1, true))
assert(output:find('\\[ \\]', 1, true), 'notes must not accidentally become editable checklists')
assert(Notes.write(path, 'Title', '', {}))
assert(read(path):find('No highlights or notes', 1, true))
assert(not Notes.write('/nonexistent-buddypoint-test/notes.md', '', '', {}))
os.remove(path)
local db = { readSetting = function(_, name)
    if name == 'annotations' then return annotations end
    if name == 'doc_props' then return {title='Live notes'} end
end }
assert(Notes.annotations(db, {}) ~= annotations, 'empty live annotations override saved data')
assert(Notes.annotations(db) == annotations)
local legacy = {readSetting=function(_,name)
    if name=='highlight' then return {[2]={{text='Old quote', datetime='date'}}} end
    if name=='bookmarks' then return {{datetime='date', note='Old note'}} end
end}
assert(Notes.annotations(legacy)[1].note == 'Old note')

local plugin = dofile('koreader-plugin/buddysync.koplugin/main.lua')
plugin.ui = {document={file='/books/live.epub'}, doc_settings=db, annotation={annotations=annotations}}
local temporary
local client = {uploadBookToX3=function(_, file, name, options)
    temporary=file
    assert(options.notes and name:match('%.md$'))
    assert(read(file):find('Café', 1, true))
    return false, 'device unavailable'
end}
local sent, reason = plugin:transferNotes('/books/live.epub', client)
assert(not sent and reason == 'device unavailable')
assert(io.open(temporary,'rb') == nil, 'temporary note file must be removed on upload failure')

local http = package.loaded['socket.http']
local upload = require('sync_client'):new('http://192.0.2.1/')
path = os.tmpname(); local f=assert(io.open(path,'wb')); f:write('# Notes\n'); f:close()
http.request=function(request)
    assert(request.url=='http://192.0.2.1/api/upload?overwrite=true&path=%2FBuddyNotes')
    local body=''
    while true do local chunk=request.source(); if not chunk then break end; body=body..chunk end
    assert(body:find('Content-Type: text/markdown; charset=utf-8',1,true))
    assert(body:find('# Notes\n',1,true))
    assert(#body==tonumber(request.headers['Content-Length']))
    return 1,200
end
assert(upload:uploadBookToX3(path,'Notes.md',{notes=true}))
assert(not upload:uploadBookToX3(path,'bad\r\nname.md',{notes=true}))
os.remove(path)
print('PASS: notes export, live/legacy annotations, empty snapshots, names, cleanup, and Markdown upload')

local notices, sent_types = {}, {}
package.loaded['ui/uimanager'].scheduleIn=function(_, _, callback) callback() end
package.loaded['ui/uimanager'].show=function(_, widget) notices[#notices+1]=widget.text end
plugin.settings={optimize_epub=false, sync_notes_with_books=true}
plugin.client={uploadBookToX3=function(_,file,name,options)
    sent_types[#sent_types+1]=options and 'notes' or 'book'
    return true,'sent'
end}
plugin:sendCurrentBook()
assert(table.concat(sent_types,',')=='book,notes')
assert(notices[#notices]:find('Notes sent too.',1,true))
sent_types={}
plugin.client.uploadBookToX3=function(_,file,name,options)
    sent_types[#sent_types+1]=options and 'notes' or 'book'
    return not options, 'notes unavailable'
end
plugin:sendCurrentBook()
assert(notices[#notices]:find('Book sent, but notes failed',1,true))
print('PASS: book transfers send notes when enabled and report partial success accurately')
local failed_temp
local success, error_message = plugin:transferNotes('/books/live.epub', {
    uploadBookToX3=function(_, file) failed_temp=file; error('unexpected transport error') end,
})
assert(not success and error_message:find('unexpected transport error',1,true))
assert(io.open(failed_temp,'rb') == nil)
print('PASS: unexpected transport errors are reported and temporary exports are removed')
