local here=debug.getinfo(1).source:match('^@(.*/)') or './'
local XML=dofile(here..'../koreader-plugin/buddysync.koplugin/epub_xml.lua')
local text=[=[<?xml version="1.0"?><!DOCTYPE package [<!ENTITY example "x > y">]>
<o:package xmlns:o="example"><!-- <item href="fake"/> --><o:manifest>
<o:item id='a' href='Images/A%20&amp;%20B.jpg' media-type="image/jpeg"></o:item>
<o:item id="b" href="keep.jpg"/>
</o:manifest><style><![CDATA[.a {content: "<x>"}]]></style></o:package>]=]
local nodes=XML.parse(text)
local item, discarded
for _,n in ipairs(nodes) do
    if XML.attr(n,'id')=='a' then item=n elseif XML.attr(n,'id')=='b' then discarded=n end
end
assert(XML.attr(item,'href')=='Images/A%20&%20B.jpg')
local patches={{discarded.start,discarded.finish,''}}
XML.set(item,'href','Images/new & different.png',patches,text)
XML.set(item,'media-type','image/png',patches,text)
local updated=XML.apply(text,patches)
assert(updated:find("href='Images/new &amp; different.png'",1,true))
assert(not updated:find('keep.jpg',1,true))
assert(updated:find('<!-- <item href="fake"/> -->',1,true))
assert(updated:find('<![CDATA[.a {content: "<x>"}]]>',1,true))
XML.parse(updated)
assert(XML.decode('&#38; &#x26; &quot; &apos;')=='& & " '.."'")
for _,bad in ipairs({'<x>','<x></y>','<x attr=no/>','<x attr="a" attr="b"/>','<!--','<![CDATA['}) do
    assert(not pcall(XML.parse,bad),'Malformed XML was accepted: '..bad)
end
print('PASS: preserving XML edits handle namespaces, quoted attributes, comments, CDATA, entities and malformed markup')
