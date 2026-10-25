-- Save as UTF-8 at moonloader/lib/thaichat.lua.
-- Requires ThaiChatBridge.asi and the supported _chat.asi.
-- Thai input must be UTF-8. Other bytes pass through unchanged.
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

return M
