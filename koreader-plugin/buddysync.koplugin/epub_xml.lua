-- A preserving XML editor for EPUB attributes and element removal.
-- It records source spans; untouched markup, namespaces, comments and CDATA stay intact.
local XML = {}
local entities = {amp = '&', lt = '<', gt = '>', quot = '"', apos = "'"}
local function utf8(n)
    if n < 128 then return string.char(n) end
    if n < 2048 then return string.char(192 + math.floor(n / 64), 128 + n % 64) end
    if n < 65536 then return string.char(224 + math.floor(n / 4096), 128 + math.floor(n / 64) % 64, 128 + n % 64) end
    if n <= 1114111 then return string.char(240 + math.floor(n / 262144), 128 + math.floor(n / 4096) % 64, 128 + math.floor(n / 64) % 64, 128 + n % 64) end
    error('Invalid XML character reference', 0)
end
function XML.decode(s)
    return (s:gsub('&([^;]+);', function(e)
        if entities[e] then return entities[e] end
        local n = e:match('^#x(%x+)$')
        n = n and tonumber(n, 16) or tonumber(e:match('^#(%d+)$'))
        return n and utf8(n) or '&' .. e .. ';'
    end))
end
function XML.escape(s)
    return (s:gsub('&', '&amp;'):gsub('<', '&lt;'):gsub('>', '&gt;'):gsub('"', '&quot;'):gsub("'", '&apos;'))
end
function XML.parse(text)
    local nodes, stack, pos = {}, {}, 1
    while true do
        local start = text:find('<', pos, true)
        if not start then break end
        local stop
        if text:sub(start, start + 3) == '<!--' then
            stop = text:find('-->', start + 4, true)
            assert(stop, 'Unclosed XML comment'); pos = stop + 3
        elseif text:sub(start, start + 8) == '<![CDATA[' then
            stop = text:find(']]>', start + 9, true)
            assert(stop, 'Unclosed XML CDATA'); pos = stop + 3
        elseif text:sub(start, start + 1) == '<?' then
            stop = text:find('?>', start + 2, true)
            assert(stop, 'Unclosed XML instruction'); pos = stop + 2
        else
            local quote, brackets = nil, 0
            for i = start + 1, #text do
                local c = text:sub(i, i)
                if quote then
                    if c == quote then quote = nil end
                elseif c == '"' or c == "'" then quote = c
                elseif c == '[' then brackets = brackets + 1
                elseif c == ']' then brackets = brackets - 1
                elseif c == '>' and brackets == 0 then stop = i; break end
            end
            assert(stop, 'Unclosed XML tag')
            local tag = text:sub(start, stop)
            if tag:sub(1, 2) == '</' then
                local name = tag:match('^</%s*([%w_:%.%-]+)%s*>$')
                local node = table.remove(stack)
                assert(node and node.name == name, 'Mismatched XML closing tag: ' .. tag)
                node.finish = stop
            elseif tag:sub(1, 2) ~= '<!' then
                local name = tag:match('^<([%w_:%.%-]+)')
                assert(name, 'Invalid XML tag')
                local node = {name = name, localname = name:match('([^:]+)$'), start = start,
                    tag_end = stop, attrs = {}, parent = stack[#stack]}
                local offset = start + #name + 1
                while offset < stop do
                    local a, b, key = text:find('^%s+([%w_:%.%-]+)%s*=%s*', offset)
                    if not a then
                        assert(text:sub(offset, stop):match('^%s*/?>$'), 'Invalid XML attributes in ' .. name)
                        break
                    end
                    local q = text:sub(b + 1, b + 1)
                    assert(q == '"' or q == "'", 'Unquoted XML attribute in ' .. name)
                    local last = assert(text:find(q, b + 2, true), 'Unclosed XML attribute')
                    assert(not node.attrs[key], 'Duplicate XML attribute: ' .. key)
                    node.attrs[key] = {value = XML.decode(text:sub(b + 2, last - 1)), first = b + 2, last = last - 1}
                    offset = last + 1
                end
                nodes[#nodes + 1] = node
                if tag:match('/%s*>$') then node.finish = stop else stack[#stack + 1] = node end
            end
            pos = stop + 1
        end
    end
    assert(#stack == 0, 'Unclosed XML element')
    return nodes
end
function XML.attr(node, name)
    return node.attrs[name] and node.attrs[name].value
end
function XML.set(node, name, value, patches, text)
    local attr = node.attrs[name]
    if attr then
        patches[#patches + 1] = {attr.first, attr.last, XML.escape(value)}
    else
        local pos = node.tag_end
        if text:sub(pos - 1, pos - 1) == '/' then pos = pos - 1 end
        patches[#patches + 1] = {pos, pos - 1, ' ' .. name .. '="' .. XML.escape(value) .. '"'}
    end
end
function XML.apply(text, patches)
    table.sort(patches, function(a, b) return a[1] > b[1] end)
    local last = #text + 1
    for _, p in ipairs(patches) do
        assert(p[2] < last, 'Overlapping EPUB edits')
        text = text:sub(1, p[1] - 1) .. p[3] .. text:sub(p[2] + 1)
        last = p[1]
    end
    return text
end
return XML
