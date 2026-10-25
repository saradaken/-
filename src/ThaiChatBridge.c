#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <wchar.h>
#include <float.h>
#include "MinHook.h"

/* ABI verified against the supplied _chat.asi, not a public ImGui header. */
typedef struct ChatFontConfig {
    void *FontData;int FontDataSize;BYTE FontDataOwnedByAtlas;BYTE pad0[3];int FontNo;
    float SizePixels;int OversampleH,OversampleV;BYTE PixelSnapH;BYTE pad1[3];
    float GlyphExtraSpacing[2],GlyphOffset[2];const uint32_t *GlyphRanges;
    float GlyphMinAdvanceX,GlyphMaxAdvanceX;BYTE MergeMode;BYTE pad2[3];
    unsigned FontBuilderFlags;float RasterizerMultiply;uint32_t EllipsisChar;
    char Name[40];void *DstFont;
} ChatFontConfig;
_Static_assert(sizeof(ChatFontConfig)==0x78,"_chat config size");
_Static_assert(offsetof(ChatFontConfig,MergeMode)==0x3C,"_chat MergeMode offset");
_Static_assert(offsetof(ChatFontConfig,GlyphRanges)==0x30,"_chat glyph range pointer");
_Static_assert(offsetof(ChatFontConfig,DstFont)==0x74,"_chat font destination");
typedef void* (__attribute__((thiscall)) *AddMemoryFontFn)(void*,void*,int,float,const ChatFontConfig*,const uint32_t*);
typedef int (WINAPI *ConvertFn)(UINT,DWORD,LPCCH,int,LPWSTR,int);
static HMODULE g_module;
static BYTE *g_chat;
static FILE *g_log;
static CRITICAL_SECTION g_lock;
static AddMemoryFontFn g_addMemory;
static ConvertFn g_originalConvert;
static void *g_fontData;static DWORD g_fontBytes;
static volatile LONG g_messages,g_merges,g_fontCalls,g_mergeFailures;
static const uint32_t g_thaiRanges[]={0x0E01,0x0E3A,0x0E3F,0x0E5B,0};
static const uint32_t g_defaultRanges[]={0x20,0xFF,0x400,0x4FF,0x2000,0x200C,0x200E,0x203B,0x203E,0x206F,0x2100,0x214F,0xFFFD,0xFFFD,0};
static void logline(const char *fmt,...){va_list a;EnterCriticalSection(&g_lock);if(g_log){va_start(a,fmt);vfprintf(g_log,fmt,a);va_end(a);fputc('\n',g_log);fflush(g_log);}LeaveCriticalSection(&g_lock);}
static BOOL readmem(const void *p,void *out,SIZE_T bytes){SIZE_T got;return p&&ReadProcessMemory(GetCurrentProcess(),p,out,bytes,&got)&&got==bytes;}
static BOOL validThaiByte(BYTE c){return(c>=0xA1&&c<=0xDA)||(c>=0xDF&&c<=0xFB);}
static WCHAR *decodeMarker(const char *s,size_t n,UINT codepage,size_t *outUnits){
    WCHAR *out;size_t i=0,o=0;BOOL changed=FALSE;
    if(n<7||n>32768)return NULL;
    out=HeapAlloc(GetProcessHeap(),0,(n+1)*sizeof(WCHAR));if(!out)return NULL;
    while(i<n){
        if(i+7<=n&&!memcmp(s+i,"[[th:",5)){
            size_t end=i+5,k;BOOL valid=TRUE;
            while(end+1<n&&!(s[end]==']'&&s[end+1]==']'))end++;
            if(end+1>=n||end==i+5)valid=FALSE;
            if(valid)for(k=i+5;k<end;k++)if(s[k]!=' '&&!validThaiByte((BYTE)s[k])){valid=FALSE;break;}
            if(valid){for(k=i+5;k<end;k++)out[o++]=s[k]==' '?L' ':(WCHAR)(0xE00+(BYTE)s[k]-0xA0);i=end+2;changed=TRUE;continue;}
        }
        {size_t begin=i;int units;i++;
            while(i<n&&!(i+5<=n&&!memcmp(s+i,"[[th:",5)))i++;
            units=g_originalConvert(codepage,0,s+begin,(int)(i-begin),out+o,(int)(n-o));
            if(units<=0){HeapFree(GetProcessHeap(),0,out);return NULL;}o+=(size_t)units;
        }
    }
    if(!changed){HeapFree(GetProcessHeap(),0,out);return NULL;}
    out[o]=0;*outUnits=o;return out;
}
/* Only _chat.asi's import slot points here. Other modules keep their imports. */
static int WINAPI convertChat(UINT cp,DWORD flags,LPCCH input,int bytes,LPWSTR output,int capacity){
    size_t n,i,units=0;BOOL marker=FALSE;WCHAR *decoded;int required;DWORD saved;
    if(!input||bytes==0||bytes < -1||capacity<0||(!output&&capacity!=0)||(const void*)input==(const void*)output)
        return g_originalConvert(cp,flags,input,bytes,output,capacity);
    if(cp!=1251&&cp!=CP_ACP&&cp!=CP_THREAD_ACP)return g_originalConvert(cp,flags,input,bytes,output,capacity);
    if(flags&~(MB_PRECOMPOSED|MB_ERR_INVALID_CHARS))return g_originalConvert(cp,flags,input,bytes,output,capacity);
    n=bytes==-1?strlen(input):(size_t)bytes;if(n>32768)return g_originalConvert(cp,flags,input,bytes,output,capacity);
    for(i=0;i+5<=n;i++)if(!memcmp(input+i,"[[th:",5)){marker=TRUE;break;}
    if(!marker)return g_originalConvert(cp,flags,input,bytes,output,capacity);
    saved=GetLastError();
    if(g_originalConvert(cp,flags,input,bytes,NULL,0)<=0)return g_originalConvert(cp,flags,input,bytes,output,capacity);
    decoded=decodeMarker(input,n,cp,&units);
    if(!decoded)return g_originalConvert(cp,flags,input,bytes,output,capacity);
    required=(int)units+(bytes==-1?1:0);
    if(!output||capacity==0){HeapFree(GetProcessHeap(),0,decoded);SetLastError(saved);return required;}
    if(capacity<required){HeapFree(GetProcessHeap(),0,decoded);SetLastError(ERROR_INSUFFICIENT_BUFFER);return 0;}
    memcpy(output,decoded,(size_t)required*sizeof(WCHAR));HeapFree(GetProcessHeap(),0,decoded);SetLastError(saved);
    InterlockedIncrement(&g_messages);return required;
}
/* Use the selected font first; fallback ranges contain missing codepoints only. */
typedef int (__cdecl *FTInitFn)(void**);
typedef int (__cdecl *FTFaceFn)(void*,const unsigned char*,long,long,void**);
typedef int (__cdecl *FTSelectFn)(void*,unsigned int);
typedef unsigned (__cdecl *FTIndexFn)(void*,unsigned long);
typedef int (__cdecl *FTDoneFn)(void*);
static FTInitFn g_ftInit;static FTFaceFn g_ftFace;static FTIndexFn g_ftIndex;static FTSelectFn g_ftSelect;
static FTDoneFn g_ftDoneFace,g_ftDoneLibrary;
static const uint32_t g_selectedRanges[]={0x20,0xFF,0x400,0x4FF,0xE01,0xE3A,0xE3F,0xE5B,0x2000,0x200C,0x200E,0x203B,0x203E,0x206F,0x2100,0x214F,0xFFFD,0xFFFD,0};
typedef struct FontPlan {ULONGLONG hash;int bytes,face;unsigned covered,missing;uint32_t ranges[175];} FontPlan;
static FontPlan g_plans[512];static unsigned g_planCount;
static volatile LONG g_selectedThai,g_selectedFallback;
static void initFreeType(void){
    HMODULE ft=GetModuleHandleA("freetype.dll");
    if(!ft){logline("FreeType coverage API unavailable; retaining Tahoma fallback");return;}
    g_ftInit=(FTInitFn)(uintptr_t)GetProcAddress(ft,"FT_Init_FreeType");
    g_ftFace=(FTFaceFn)(uintptr_t)GetProcAddress(ft,"FT_New_Memory_Face");
    g_ftSelect=(FTSelectFn)(uintptr_t)GetProcAddress(ft,"FT_Select_Charmap");
    g_ftIndex=(FTIndexFn)(uintptr_t)GetProcAddress(ft,"FT_Get_Char_Index");
    g_ftDoneFace=(FTDoneFn)(uintptr_t)GetProcAddress(ft,"FT_Done_Face");
    g_ftDoneLibrary=(FTDoneFn)(uintptr_t)GetProcAddress(ft,"FT_Done_FreeType");
    logline("Selected-font coverage API ready=%d",g_ftInit&&g_ftFace&&g_ftIndex&&g_ftSelect&&g_ftDoneFace&&g_ftDoneLibrary);
}
static FontPlan *fontPlan(void *data,int bytes,int face){
    ULONGLONG hash=14695981039346656037ULL;FontPlan *p=NULL;void *library=NULL,*font=NULL;unsigned i,at=0;BOOL run=FALSE;uint32_t c;
    if(!data||bytes<=0||bytes>16*1024*1024||!g_ftInit||!g_ftFace||!g_ftIndex||!g_ftSelect||!g_ftDoneFace||!g_ftDoneLibrary)return NULL;
    for(i=0;i<(unsigned)bytes;i++){hash^=((const BYTE*)data)[i];hash*=1099511628211ULL;}
    EnterCriticalSection(&g_lock);
    for(i=0;i<g_planCount;i++)if(g_plans[i].hash==hash&&g_plans[i].bytes==bytes&&g_plans[i].face==face){p=&g_plans[i];goto done;}
    if(g_planCount>=512)goto done;
    if(g_ftInit(&library)||!library)goto done;
    if(g_ftFace(library,data,bytes,face,&font)||!font){g_ftDoneLibrary(library);goto done;}
    if(g_ftSelect(font,0x756E6963U)){g_ftDoneFace(font);g_ftDoneLibrary(library);goto done;}
    p=&g_plans[g_planCount];memset(p,0,sizeof(*p));p->hash=hash;p->bytes=bytes;p->face=face;
    for(c=0xE01;c<=0xE5B;c++){
        BOOL thai=(c<=0xE3A||c>=0xE3F);BOOL missing=thai&&g_ftIndex(font,c)==0;
        if(thai){if(missing)p->missing++;else p->covered++;}
        if(missing){if(!run){p->ranges[at++]=c;p->ranges[at++]=c;run=TRUE;}else p->ranges[at-1]=c;}
        else run=FALSE;
    }
    p->ranges[at]=0;g_planCount++;g_ftDoneFace(font);g_ftDoneLibrary(library);
    logline("Selected font Thai coverage: native=%u missing=%u",p->covered,p->missing);
done:
    LeaveCriticalSection(&g_lock);return p;
}

/* Runs when _chat itself adds its text fonts, on _chat's own thread. */
static void* __attribute__((fastcall)) addFont(void *atlas,void *unused,void *data,int bytes,float pixels,const ChatFontConfig *config,const uint32_t *ranges){
    void *primary,*fallback;ChatFontConfig thai;FontPlan *plan;const uint32_t *missingRanges=g_thaiRanges;(void)unused;
    BOOL textFont=(!config||!config->MergeMode)&&(!ranges||ranges==(const uint32_t*)(g_chat+0xD6D24))&&pixels>0.0f&&pixels<=128.0f;
    InterlockedIncrement(&g_fontCalls);
    primary=g_addMemory(atlas,data,bytes,pixels,config,textFont?g_selectedRanges:ranges);
    if(!primary||!g_fontData||!textFont)return primary;
    plan=fontPlan(data,bytes,config?config->FontNo:0);
    if(plan){
        if(plan->covered)InterlockedIncrement(&g_selectedThai);
        if(!plan->missing)return primary;
        missingRanges=plan->ranges;
    }
    InterlockedIncrement(&g_selectedFallback);
    if(config)memcpy(&thai,config,sizeof(thai));
    else {memset(&thai,0,sizeof(thai));thai.OversampleH=3;thai.OversampleV=1;thai.GlyphMaxAdvanceX=FLT_MAX;thai.RasterizerMultiply=1.0f;thai.EllipsisChar=0xFFFFFFFF;}
    thai.FontData=NULL;thai.FontDataSize=0;
    /* Zero makes _chat copy font data using its own allocator (RVA 6CFA2). */
    thai.FontDataOwnedByAtlas=0;thai.FontNo=0;thai.MergeMode=1;
    thai.SizePixels=pixels;thai.GlyphRanges=missingRanges;thai.DstFont=primary;
    memset(thai.Name,0,sizeof(thai.Name));memcpy(thai.Name,"Thai fallback (Tahoma)",sizeof("Thai fallback (Tahoma)")-1);
    fallback=g_addMemory(atlas,g_fontData,(int)g_fontBytes,pixels,&thai,missingRanges);
    if(fallback){LONG count=InterlockedIncrement(&g_merges);if(count<=8)logline("Missing Thai glyph fallback registered: size=%.1f total=%ld",(double)pixels,count);}
    else {InterlockedIncrement(&g_mergeFailures);logline("Thai atlas merge failed: size=%.1f",(double)pixels);}
    return primary;
}
static BOOL loadFont(void){
    WCHAR path[MAX_PATH];HANDLE f;LARGE_INTEGER size;DWORD got=0;void *data;
    UINT n=GetWindowsDirectoryW(path,MAX_PATH);if(!n||n>MAX_PATH-32)return FALSE;
    wcscat(path,L"\\Fonts\\tahoma.ttf");f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(f==INVALID_HANDLE_VALUE){logline("Cannot open Windows Tahoma font: error=%lu",GetLastError());return FALSE;}
    if(!GetFileSizeEx(f,&size)||size.QuadPart<256||size.QuadPart>16*1024*1024){CloseHandle(f);return FALSE;}
    data=HeapAlloc(GetProcessHeap(),0,(SIZE_T)size.QuadPart);if(!data){CloseHandle(f);return FALSE;}
    if(!ReadFile(f,data,(DWORD)size.QuadPart,&got,NULL)||got!=(DWORD)size.QuadPart){HeapFree(GetProcessHeap(),0,data);CloseHandle(f);return FALSE;}
    CloseHandle(f);
    if(memcmp(data,"\0\1\0\0",4)&&memcmp(data,"OTTO",4)){HeapFree(GetProcessHeap(),0,data);return FALSE;}
    g_fontData=data;g_fontBytes=got;logline("Windows Tahoma loaded: bytes=%lu",got);return TRUE;
}
static BOOL validChat(BYTE *base){
    IMAGE_DOS_HEADER d;IMAGE_NT_HEADERS32 nt;BYTE prologue[9];uint32_t ranges[15];
    static const BYTE expected[]={0x55,0x8B,0xEC,0x81,0xEC,0xFC,0,0,0};
    if(!readmem(base,&d,sizeof(d))||d.e_magic!=IMAGE_DOS_SIGNATURE||d.e_lfanew<0x40||d.e_lfanew>0x1000)return FALSE;
    if(!readmem(base+d.e_lfanew,&nt,sizeof(nt))||nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386)return FALSE;
    if(nt.FileHeader.TimeDateStamp!=0x6A9589F5||nt.OptionalHeader.AddressOfEntryPoint!=0xC32FA||nt.OptionalHeader.SizeOfImage!=0x1CB000)return FALSE;
    if(!readmem(base+0x6D4A0,prologue,sizeof(prologue))||memcmp(prologue,expected,sizeof(expected)))return FALSE;
    if(!readmem(base+0xD6D24,ranges,sizeof(ranges))||memcmp(ranges,g_defaultRanges,sizeof(ranges)))return FALSE;
    return TRUE;
}
static BOOL install(void){
    ConvertFn original=NULL;void **slot=(void**)(g_chat+0xCA0F4);MH_STATUS status;DWORD old,unused;
    if(!readmem(slot,&original,sizeof(original))||!original)return FALSE;
    if(!loadFont()){logline("No valid Thai font; installation stopped");return FALSE;}
    initFreeType();
    status=MH_Initialize();if(status!=MH_OK){logline("MinHook init failed: %s",MH_StatusToString(status));return FALSE;}
    status=MH_CreateHook(g_chat+0x6D4A0,(void*)&addFont,(void**)&g_addMemory);
    if(status!=MH_OK){logline("_chat AddFont hook failed: %s",MH_StatusToString(status));return FALSE;}
    status=MH_EnableHook(g_chat+0x6D4A0);
    if(status!=MH_OK){logline("_chat font hook activation failed: %s",MH_StatusToString(status));MH_RemoveHook(g_chat+0x6D4A0);return FALSE;}
    /* Publish conversion only after the atlas hook can add Thai glyphs. */
    g_originalConvert=original;
    if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old)){MH_DisableHook(g_chat+0x6D4A0);logline("_chat IAT page is not writable");return FALSE;}
    InterlockedExchangePointer(slot,(void*)&convertChat);
    if(!VirtualProtect(slot,sizeof(void*),old,&unused))logline("Restoring _chat IAT protection failed");
    logline("Installed: _chat import MultiByteToWideChar + _chat AddFontFromMemoryTTF only");
    return TRUE;
}
static DWORD WINAPI worker(void *arg){
    WCHAR path[MAX_PATH];HMODULE pin;unsigned i;(void)arg;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,(LPCSTR)&worker,&pin);
    if(!GetModuleFileNameW(g_module,path,MAX_PATH-5))return 1;
    {WCHAR *dot=wcsrchr(path,L'.');if(dot)*dot=0;wcscat(path,L".log");}g_log=_wfopen(path,L"wb");
    if(!g_log){GetTempPathW(MAX_PATH-32,path);wcscat(path,L"ThaiChatBridge.log");g_log=_wfopen(path,L"wb");}
    logline("ThaiChatBridge 1.1 experimental selected-font _chat.asi companion; no D3DX or global conversion hooks");
    for(i=0;i<240;i++){g_chat=(BYTE*)GetModuleHandleA("_chat.asi");if(g_chat)break;Sleep(250);}
    if(!g_chat){logline("_chat.asi not loaded within 60 seconds");return 2;}
    if(!validChat(g_chat)){logline("Unsupported or modified _chat build; no hooks installed");return 3;}
    if(!install()){logline("Installation failed");return 4;}
    for(i=0;;i++){
        if(i%40==0)logline("messages=%ld fontCalls=%ld fontMerges=%ld mergeFailures=%ld selectedThai=%ld selectedFallback=%ld",g_messages,g_fontCalls,g_merges,g_mergeFailures,g_selectedThai,g_selectedFallback);
        Sleep(250);
    }
    return 0;
}
__declspec(dllexport) const char *ThaiChatBridgeVersion(void){return "1.1 selected-font _chat companion";}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,void *reserved){(void)reserved;if(reason==DLL_PROCESS_ATTACH){HANDLE t;g_module=module;InitializeCriticalSection(&g_lock);DisableThreadLibraryCalls(module);t=CreateThread(NULL,0,worker,NULL,0,NULL);if(t)CloseHandle(t);}return TRUE;}
