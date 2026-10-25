# ThaiNative ASI Probe

แพ็กเกจทดลองสำหรับตรวจเส้นทางวาดแชทของ SA-MP 0.3.7-R3 บน Windows 32 บิต

**ไฟล์นี้เป็นตัวตรวจสอบ ยังไม่ใช่ปลั๊กอินแสดงภาษาไทยที่ทำเสร็จแล้ว และยังไม่ได้ทดสอบในเกมจริง**

[ดาวน์โหลด ThaiNative_ASI_Probe.zip](https://github.com/saradaken/-/raw/refs/heads/main/downloads/ThaiNative_ASI_Probe.zip)

## วิธีทดสอบ

1. ปิดเกม และนำสคริปต์ ThaiNative/CEFThai รุ่นทดลองก่อนหน้าออกจาก `moonloader` เพื่อไม่ให้ hook ซ้ำกัน
2. แตก ZIP แล้ววาง `ThaiNativeProbe.asi` ข้าง `gta_sa.exe` ต้องมี ASI Loader ที่ใช้งานได้อยู่แล้ว
3. วาง `ThaiNativeProbeTest.lua` ใน `moonloader`
4. เข้าเกม พิมพ์ `/thasi` รอ 15 วินาที แล้วออกเกม
5. นำ `ThaiNativeProbe.log` ที่อยู่ข้าง `.asi` มาตรวจ ถ้าสร้างในโฟลเดอร์เกมไม่ได้ ไฟล์จะอยู่ที่ `%TEMP%\ThaiNativeProbe.log`

การเห็น `~THAI_ASI_TEST~` ในขั้นทดสอบนี้เป็นเรื่องปกติ ยังไม่มีการถอดรหัสเป็นภาษาไทย

ZIP มีไฟล์ ASI, สคริปต์ทดสอบ, ซอร์ส C, คำสั่ง build และ MinHook 1.3.4 พร้อมใบอนุญาต ปลั๊กอินไม่แก้ `samp.dll` บนดิสก์ ไม่บันทึกข้อความแชททั่วไป และไม่ส่งข้อมูลออกเครือข่าย

## การตรวจสอบที่ทำแล้ว

- คอมไพล์เป็น PE32 DLL x86 ด้วย MinGW-w64 GCC
- ซอร์สปลั๊กอินผ่าน `-Wall -Wextra -Werror`
- ทดสอบการส่งอาร์กิวเมนต์และ stack ของ callback ที่คอมไพล์แล้วด้วยตัวจำลอง x86
- ยังไม่ได้ตรวจการทำงานบน Windows/SA-MP จริง

SHA-256 ของ ZIP:

`dcf8db4cd6e4909d9b8970a9e5aaf4333ea1cea8d3840c079228323eb05f8d58`
