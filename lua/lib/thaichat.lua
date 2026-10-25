-- Save as UTF-8 at moonloader/lib/thaichat.lua.
-- Requires ThaiChatBridge.asi and the supported _chat.asi.
-- add: Thai input is UTF-8; other bytes pass through unchanged.
-- addUtf8: the entire message is UTF-8, including Russian.
local M = {}
local nativeAddChatMessage = sampAddChatMessage

local function thaiPack(s)
    local out, run, i = {}, {}, 1
    local function flush()
        if #run > 0 then
            out[#out + 1] = '[[th:' .. table.concat(run) .. ']]'
            run = {}
        end
    end
    while i <= #s do
        local a, b, c = s:byte(i, i + 2)
        local off = a == 0xE0 and (b == 0xB8 or b == 0xB9)
            and c and c >= 0x80 and c <= 0xBF
            and ((b - 0xB8) * 64 + c - 0x80) or nil
        if off and off >= 1 and off <= 0x5B then
            run[#run + 1] = string.char(0xA0 + off)
            i = i + 3
        elseif a == 32 and #run > 0 then
            run[#run + 1] = ' '
            i = i + 1
        else
            flush()
            out[#out + 1] = s:sub(i, i)
            i = i + 1
        end
    end
    flush()
    return table.concat(out)
end



M.pack = thaiPack

function M.add(text, color)
    return nativeAddChatMessage(thaiPack(text), color or -1)
end

-- UTF-8 source mode: converts Cyrillic/Windows-1251 characters and packs Thai
-- in one pass. Never pass already-decoded CP1251 bytes to this mode.
local cp1251Extra = {
    [0x0402]=0x80, [0x0403]=0x81, [0x201A]=0x82, [0x0453]=0x83, [0x201E]=0x84,
    [0x2026]=0x85, [0x2020]=0x86, [0x2021]=0x87, [0x20AC]=0x88, [0x2030]=0x89,
    [0x0409]=0x8A, [0x2039]=0x8B, [0x040A]=0x8C, [0x040C]=0x8D, [0x040B]=0x8E,
    [0x040F]=0x8F, [0x0452]=0x90, [0x2018]=0x91, [0x2019]=0x92, [0x201C]=0x93,
    [0x201D]=0x94, [0x2022]=0x95, [0x2013]=0x96, [0x2014]=0x97, [0x2122]=0x99,
    [0x0459]=0x9A, [0x203A]=0x9B, [0x045A]=0x9C, [0x045C]=0x9D, [0x045B]=0x9E,
    [0x045F]=0x9F, [0x00A0]=0xA0, [0x040E]=0xA1, [0x045E]=0xA2, [0x0408]=0xA3,
    [0x00A4]=0xA4, [0x0490]=0xA5, [0x00A6]=0xA6, [0x00A7]=0xA7, [0x0401]=0xA8,
    [0x00A9]=0xA9, [0x0404]=0xAA, [0x00AB]=0xAB, [0x00AC]=0xAC, [0x00AD]=0xAD,
    [0x00AE]=0xAE, [0x0407]=0xAF, [0x00B0]=0xB0, [0x00B1]=0xB1, [0x0406]=0xB2,
    [0x0456]=0xB3, [0x0491]=0xB4, [0x00B5]=0xB5, [0x00B6]=0xB6, [0x00B7]=0xB7,
    [0x0451]=0xB8, [0x2116]=0xB9, [0x0454]=0xBA, [0x00BB]=0xBB, [0x0458]=0xBC,
    [0x0405]=0xBD, [0x0455]=0xBE, [0x0457]=0xBF,
}

function M.packUtf8(text)
    local out, run, i = {}, {}, 1
    local function flush()
        if #run > 0 then
            out[#out + 1] = '[[th:' .. table.concat(run) .. ']]'
            run = {}
        end
    end
    while i <= #text do
        local first = text:byte(i)
        local cp, width, minimum = first, 1, 0
        if first >= 0x80 then
            if first >= 0xC2 and first <= 0xDF then cp, width, minimum = first - 0xC0, 2, 0x80
            elseif first >= 0xE0 and first <= 0xEF then cp, width, minimum = first - 0xE0, 3, 0x800
            elseif first >= 0xF0 and first <= 0xF4 then cp, width, minimum = first - 0xF0, 4, 0x10000
            else return nil, 'Invalid UTF-8 at byte ' .. i end
            for j = 1, width - 1 do
                local nextByte = text:byte(i + j)
                if not nextByte or nextByte < 0x80 or nextByte > 0xBF then
                    return nil, 'Invalid UTF-8 at byte ' .. i
                end
                cp = cp * 64 + nextByte - 0x80
            end
            if cp < minimum or cp > 0x10FFFF or (cp >= 0xD800 and cp <= 0xDFFF) then
                return nil, 'Invalid UTF-8 at byte ' .. i
            end
        end
        if (cp >= 0x0E01 and cp <= 0x0E3A) or (cp >= 0x0E3F and cp <= 0x0E5B) then
            run[#run + 1] = string.char(0xA0 + cp - 0x0E00)
        elseif cp == 0x20 and #run > 0 then
            run[#run + 1] = ' '
        else
            flush()
            local byte
            if cp < 0x80 then byte = cp
            elseif cp >= 0x0410 and cp <= 0x044F then byte = 0xC0 + cp - 0x0410
            else byte = cp1251Extra[cp] end
            if not byte then
                return nil, string.format('U+%04X is outside Thai/Windows-1251 support', cp)
            end
            out[#out + 1] = string.char(byte)
        end
        i = i + width
    end
    flush()
    return table.concat(out)
end

function M.addUtf8(text, color)
    local packed, err = M.packUtf8(text)
    if not packed then error('thaichat.addUtf8: ' .. err, 2) end
    return nativeAddChatMessage(packed, color or -1)
end

return M
