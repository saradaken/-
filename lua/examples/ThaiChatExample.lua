script_name('ThaiChat shared helper example')
local thai = require 'thaichat'

function main()
    while not isSampAvailable() do wait(100) end
    sampRegisterChatCommand('thsample', function()
        thai.add('{00FF00}สวัสดีครับ ข้อความนี้เป็นภาษาไทย', -1)
        thai.add('{FFD24A}ก่อน ก้อน กิ่ง กี่ น้ำ ผู้เล่น', -1)
    end)
    sampAddChatMessage('[ThaiChat] /thsample', -1)
    wait(-1)
end
