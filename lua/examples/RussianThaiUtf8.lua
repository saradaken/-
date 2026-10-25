script_name('Russian and Thai UTF-8 Chat')
local sampAddChatMessage = require('thaichat').addUtf8

function main()
    while not isSampAvailable() do wait(100) end
    sampRegisterChatCommand('th', function()
        sampAddChatMessage('{00FF00}[Система]: {FFFFFF}Привет! Добро пожаловать на сервер.', -1)
        sampAddChatMessage('{FF9900}[ข้อความ]: {FFFFFF}Как дела? (เป็นอย่างไรบ้าง?)', -1)
        sampAddChatMessage('สวัสดีครับ', -1)
    end)
    wait(-1)
end
