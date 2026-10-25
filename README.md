# ThaiChatBridge — Thai support for the supplied `_chat.asi`

[ดาวน์โหลด ThaiChatBridge_v1_1.zip](https://github.com/saradaken/-/raw/refs/heads/main/downloads/ThaiChatBridge_v1_1.zip)

**รุ่นทดลองใหม่ ยังไม่ได้รันในเกม Windows จริง** ใช้ `_chat.asi` เป็นตัววาดแชทหลัก ไม่สร้างหน้าต่างใหม่ ไม่ใช้ตัววาดฟอนต์รวมของ ThaiNative รุ่นก่อน

ThaiNative รุ่นก่อนทำให้เกมเด้งและไม่แนะนำให้โหลดร่วมกับรุ่นนี้

## รุ่น 1.1: ตามฟอนต์ที่เลือกในเมนูเดิม

ฟอนต์หลักจะได้รับช่วงภาษาไทย และตรวจ glyph ด้วย Unicode cmap ของฟอนต์นั้น หากมีไทยครบจะไม่ใช้ Tahoma หากขาดบางตัวจะใช้ Tahoma เฉพาะตัวที่ขาด เมื่อเปลี่ยนฟอนต์หรือขนาดในเมนู `_chat` จะสร้าง atlas ใหม่และ bridge ใช้ฟอนต์ที่เลือกให้อัตโนมัติ

อัปเกรดจาก 1.0 โดยปิดเกมแล้วเปลี่ยนเฉพาะ `ThaiChatBridge.asi` ใช้ Lua เดิมได้ Log จะแสดง `Selected font Thai coverage: native=... missing=...` ฟอนต์ที่ไม่มีไทยยังต้องใช้ fallback; หาก coverage API ใช้ไม่ได้จะคง fallback เดิม

ทดสอบโค้ดที่คอมไพล์ร่วมกับ AddFont จริงของ `_chat` ใน x86 emulator ผ่านกรณีมีไทยครบ/ไม่มีไทย/มีบางตัว รวมทั้งเปลี่ยนขนาดและสลับฟอนต์กลับ FreeType coverage เป็นตัวจำลองในการทดสอบ รูปทรงและวรรณยุกต์จริงต้องตรวจในเกม

## ติดตั้งเพื่อทดสอบ

1. ปิดเกม เอา `ThaiNative.asi` / `ThaiNativeProbe.asi` และสคริปต์ทดลองก่อนหน้าที่ส่งให้ ออกจากการโหลด
2. เก็บ `_chat.asi` และ `samp.dll` ของคุณไว้ตามเดิม
3. วาง `ThaiChatBridge.asi` ข้าง `gta_sa.exe` ต้องมี ASI Loader
4. วาง `ThaiChatBridgeTest.lua` ใน `moonloader` สำหรับทดสอบครั้งแรก
5. เข้าเกม รอ 5 วินาที แล้วพิมพ์ `/thtest`
6. ถ้าอักษรหาย/เป็นสี่เหลี่ยม และ log มี `fontCalls=0` ลอง `/fontsize 1` เพื่อให้ `_chat` สร้างฟอนต์ใหม่ แล้วทดสอบอีกครั้ง คำสั่งนี้เปลี่ยนขนาดฟอนต์ของแชท
7. หากยังผิดปกติ รอ 15 วินาที แล้วส่ง `ThaiChatBridge.log` กับภาพผลทดสอบ หากเด้งให้หยุดใช้แล้วส่ง crash log ด้วย

Log อยู่ข้าง `.asi` หรือ `%TEMP%\ThaiChatBridge.log` ถ้าโฟลเดอร์เกมเขียนไม่ได้

เมื่อทดสอบผ่าน ใช้ `CEFThai_ChatBridge.lua` แทนสคริปต์ทดสอบเพื่อใช้ `cef_raw.txt` เดิม โหลด Lua เพียงตัวเดียวเพราะมี `/thtest` เหมือนกัน

## หลักฐานจากไฟล์ที่ตรวจ

- `_chat.asi` เป็น ImGui + FreeType มีชุด glyph เริ่มต้น Latin/Cyrillic/punctuation แต่ไม่มีช่วงภาษาไทย
- ฟังก์ชันแปลง CP1251 เป็น UTF-16 ที่ log พบ (`_chat.asi+EFDC/F036`) เรียก import `MultiByteToWideChar` ของ `_chat`
- `samp.dll` ที่แนบล่าสุดมี SHA-256 ตรงกับไฟล์ R3 ที่ตรวจไปก่อนหน้า

รุ่นนี้เปลี่ยนเฉพาะ import slot ของ `_chat` สำหรับถอด CP874 markers และ hook การเพิ่มฟอนต์ของ `_chat` เพื่อ merge glyph ไทยจาก Windows Tahoma เข้า font atlas เดิม ให้ `_chat` วาดข้อความ สี เงา และประวัติเอง

ไม่มี global Windows API hook หรือ `DrawTextA/W` hook และไม่แก้ไฟล์ `_chat.asi`/`samp.dll` บนดิสก์ ไม่เปลี่ยนการส่งข้อความไปเซิร์ฟเวอร์

## การตรวจสอบและขอบเขต

- คอมไพล์ Windows x86 ด้วย `-Wall -Wextra -Werror`; Lua ผ่าน LuaJIT 2.1
- รัน conversion callback ที่คอมไพล์แล้วใน x86 emulator ร่วมกับ Lua pack: Thai/tones/colors/mixed Russian, malformed markers และ buffer limits ผ่าน
- รัน callback ร่วมกับโค้ด `AddFontFromMemoryTTF` และ `AddFont` จาก `_chat.asi` ที่แนบจริงใน emulator: ABI, config 120 bytes, merge ไปฟอนต์เดิม, Thai ranges, ไม่แก้ config ต้นทาง และคัดลอก fallback font ด้วย allocator ของ `_chat` ผ่าน
- ทดสอบไม่ merge ทับ icon fonts หรือ config ที่ merge อยู่แล้ว
- ยังไม่ได้ทดสอบ Windows FreeType rasterization, GPU rendering หรือความเสถียรในเกมจริง การจัดตำแหน่งวรรณยุกต์ต้องตรวจจริง และไม่ได้อ้างว่ามี Thai shaping สมบูรณ์

รองรับเฉพาะ `_chat.asi` ที่ตรวจ: SHA-256 `22b3fb9cc4fe4d36ad2b63daaa90ffbd2e6c1006180581186cd5b97849cd1f6c` ตรวจ PE metadata, function prologue และ default ranges ก่อนติดตั้ง

[ซอร์ส companion](src/ThaiChatBridge.c) · [SHA-256 ของแพ็กเกจ](downloads/SHA256SUMS.txt)

ZIP มีซอร์ส คำสั่ง build และ MinHook 1.3.4 พร้อมใบอนุญาต ไม่มีไฟล์เกมที่ผู้ใช้แนบหรือฟอนต์ Microsoft รวมอยู่

ไฟล์ ThaiNative/Probe รุ่นก่อนที่เก็บไว้ใน `downloads` เป็นงานทดลองเก่า ไม่ใช่ตัวเลือกติดตั้งปัจจุบัน

## ใช้กับสคริปต์ Lua อื่น

[ดาวน์โหลด helper และตัวอย่าง](https://github.com/saradaken/-/raw/refs/heads/main/downloads/ThaiChat_Lua_Helper.zip)

วาง `thaichat.lua` ใน `moonloader/lib` แล้วเพิ่ม `local thai = require 'thaichat'` ในสคริปต์ จากนั้นเรียก `thai.add('สวัสดีครับ', -1)`

ถ้าต้องการใช้ชื่อฟังก์ชันเดิม เพิ่ม `local sampAddChatMessage = require('thaichat').add` ก่อนฟังก์ชันที่ส่งข้อความ ขอบเขต local ต้องครอบคลุมจุดเรียกเหล่านั้น

ไฟล์สคริปต์และข้อความไทยต้องเป็น UTF-8 ไม่ส่งไทยผ่าน `u8(...)` หรือ `u8:decode(...)` อีกครั้ง Module แพ็กไทยให้ แต่ไม่แปลงข้อความรัสเซีย UTF-8 เป็น CP1251 อัตโนมัติ: ข้อความรัสเซียจากไฟล์ UTF-8 ต้องแปลงแยก เช่น `u8:decode('Привет: ') .. 'สวัสดี'` ก่อนเรียก `thai.add` โดยตั้ง default encoding เป็น CP1251

ยังต้องมี `ThaiChatBridge.asi` และ `_chat.asi` รุ่นที่รองรับ ตัวอย่างใช้คำสั่ง `/thsample` และไม่ส่งข้อความออกเซิร์ฟเวอร์
