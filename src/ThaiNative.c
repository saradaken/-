#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>
#include <string.h>
#include "MinHook.h"

typedef struct {INT Height;UINT Width,Weight,MipLevels;BOOL Italic;BYTE CharSet,OutputPrecision,Quality,PitchAndFamily;WCHAR FaceName[32];} FontDesc;
typedef HRESULT (WINAPI *FontCreateFn)(void*,const FontDesc*,void**);
typedef int (WINAPI *DrawFn)(void*,void*,const void*,INT,RECT*,DWORD,DWORD);
typedef ULONG (WINAPI *ReleaseFn)(void*);
typedef HRESULT (WINAPI *StateFn)(void*);
typedef HRESULT (WINAPI *DeviceFn)(void*,void**);
typedef HRESULT (WINAPI *DescFn)(void*,FontDesc*);
typedef struct Backend {const char *name;HMODULE module;FontCreateFn create;DrawFn drawA,drawW;ReleaseFn release;StateFn lost,reset;volatile LONG callsA,callsW,hits,rendered,failures;BOOL ready;} Backend;
typedef struct Cached {Backend *backend;void *source,*thai;struct Cached *next;} Cached;
static Backend g_backends[2]={{.name="d3dx9_25.dll"},{.name="d3dx9_43.dll"}};
static Cached *g_fonts;
static FILE *g_log;
static HMODULE g_module;
static CRITICAL_SECTION g_lock;
static volatile LONG g_bad_markers;
static DWORD g_drawTls=TLS_OUT_OF_INDEXES;
static void **vt(void *p){return *(void***)p;}
static void logline(const char *fmt,...){va_list a;EnterCriticalSection(&g_lock);if(g_log){va_start(a,fmt);vfprintf(g_log,fmt,a);va_end(a);fputc('\n',g_log);fflush(g_log);}LeaveCriticalSection(&g_lock);}
static BOOL readmem(uintptr_t p,void *out,SIZE_T n){SIZE_T got;return p&&ReadProcessMemory(GetCurrentProcess(),(void*)p,out,n,&got)&&got==n;}
static DWORD read32(uintptr_t p){DWORD v=0;readmem(p,&v,4);return v;}
static size_t textlenA(const char *s,int count){if(count>=0)return (size_t)count;return strlen(s);}
static size_t textlenW(const WCHAR *s,int count){if(count>=0)return (size_t)count;return wcslen(s);}
static BOOL thaibyte(BYTE b){return (b>=0xA1&&b<=0xDA)||(b>=0xDF&&b<=0xFB);}
/* Return NULL if no valid marker exists: all other text takes its original path. */
static WCHAR *decodeACodePage(const char *s,size_t n,size_t *units,UINT cp){
    WCHAR *out;size_t i=0,o=0;BOOL found=FALSE;
    if(n>32768 || n<7)return NULL;
    out=HeapAlloc(GetProcessHeap(),0,(n+1)*sizeof(WCHAR));if(!out)return NULL;
    while(i<n){
        if(i+7<=n&&!memcmp(s+i,"[[th:",5)){
            size_t end=i+5,k;BOOL valid=TRUE;
            while(end+1<n&&!(s[end]==']'&&s[end+1]==']'))end++;
            if(end+1>=n||end==i+5)valid=FALSE;
            if(valid)for(k=i+5;k<end;k++)if(s[k]!=' '&&!thaibyte((BYTE)s[k])){valid=FALSE;break;}
            if(valid){for(k=i+5;k<end;k++)out[o++]=s[k]==' '?L' ':(WCHAR)(0xE00+(BYTE)s[k]-0xA0);i=end+2;found=TRUE;continue;}
        }
        {size_t begin=i;int got;i++;
            while(i<n&&!(i+5<=n&&!memcmp(s+i,"[[th:",5)))i++;
            got=MultiByteToWideChar(cp,0,s+begin,(int)(i-begin),out+o,(int)(n-o));
            if(got<=0){HeapFree(GetProcessHeap(),0,out);return NULL;}o+=(size_t)got;
        }
    }
    out[o]=0;*units=o;if(!found){HeapFree(GetProcessHeap(),0,out);return NULL;}return out;
}
static WCHAR *decodeA(const char *s,size_t n,size_t *units){return decodeACodePage(s,n,units,1251);}
static int from1251(WCHAR c){char b=0;BOOL fallback=FALSE;int n;if(c==L' ')return 32;
    n=WideCharToMultiByte(1251,WC_NO_BEST_FIT_CHARS,&c,1,&b,1,NULL,&fallback);
    return n==1&&!fallback?(BYTE)b:-1;
}
static WCHAR *decodeW(const WCHAR *s,size_t n,size_t *units){
    WCHAR *out;size_t i=0,o=0;BOOL found=FALSE;
    if(n>32768||n<7)return NULL;
    out=HeapAlloc(GetProcessHeap(),0,(n+1)*sizeof(WCHAR));if(!out)return NULL;
    while(i<n){
        if(i+7<=n&&!wmemcmp(s+i,L"[[th:",5)){
            size_t end=i+5,k;BOOL valid=TRUE;
            while(end+1<n&&!(s[end]==L']'&&s[end+1]==L']'))end++;
            if(end+1>=n||end==i+5)valid=FALSE;
            if(valid)for(k=i+5;k<end;k++){int b=from1251(s[k]);if(b!=32&&(b<0||!thaibyte((BYTE)b))){valid=FALSE;break;}}
            if(valid){for(k=i+5;k<end;k++){int b=from1251(s[k]);out[o++]=b==32?L' ':(WCHAR)(0xE00+b-0xA0);}i=end+2;found=TRUE;continue;}
        }
        out[o++]=s[i++];
    }
    out[o]=0;*units=o;if(!found){HeapFree(GetProcessHeap(),0,out);return NULL;}return out;
}
static Cached *findfont(Backend *b,void *font){Cached *c;for(c=g_fonts;c;c=c->next)if(c->backend==b&&c->source==font)return c;return NULL;}
static void *thaifont(Backend *b,void *font){
    Cached *c=findfont(b,font);FontDesc desc;void *device=NULL,*created=NULL;HRESULT hr;
    if(c)return c->thai;
    if(((DescFn)vt(font)[5])(font,&desc)<0 || ((DeviceFn)vt(font)[3])(font,&device)<0)return NULL;
    memset(desc.FaceName,0,sizeof(desc.FaceName));wcscpy(desc.FaceName,L"Tahoma");desc.CharSet=DEFAULT_CHARSET;
    hr=b->create(device,&desc,&created);((ReleaseFn)vt(device)[2])(device);
    if(hr<0||!created)return NULL;
    c=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*c));
    if(!c){b->release(created);return NULL;}
    c->backend=b;c->source=font;c->thai=created;c->next=g_fonts;g_fonts=c;
    return created;
}
static int hexvalue(WCHAR c){if(c>=L'0'&&c<=L'9')return c-L'0';if(c>=L'a'&&c<=L'f')return c-L'a'+10;if(c>=L'A'&&c<=L'F')return c-L'A'+10;return -1;}
static BOOL colortag(const WCHAR *s,size_t n,DWORD *rgb){size_t j;DWORD v=0;if(n<8||s[0]!=L'{'||s[7]!=L'}')return FALSE;for(j=1;j<=6;j++){int h=hexvalue(s[j]);if(h<0)return FALSE;v=(v<<4)|(DWORD)h;}*rgb=v;return TRUE;}
static int drawthai(Backend *b,void *source,void *sprite,WCHAR *s,size_t n,RECT *rect,DWORD flags,DWORD color){
    void *font=thaifont(b,source);size_t i,o=0;BOOL tags=FALSE;WCHAR *plain;DWORD rgb;int result=0;
    if(!font){b->failures++;return -1;}
    plain=HeapAlloc(GetProcessHeap(),0,(n+1)*sizeof(WCHAR));if(!plain){b->failures++;return -1;}
    for(i=0;i<n;){if(colortag(s+i,n-i,&rgb)){i+=8;tags=TRUE;}else plain[o++]=s[i++];}plain[o]=0;
    if((flags&DT_CALCRECT)||!tags){result=b->drawW(font,sprite,plain,(int)o,rect,flags,color);}
    else {
        size_t begin=0;LONG x=rect->left;DWORD current=color;BOOL shadow=(color&0xFFFFFF)==0;
        for(i=0;i<=n;i++){
            BOOL tag=i<n&&colortag(s+i,n-i,&rgb);
            if(i==n||tag){
                if(i>begin){RECT measure={0,0,0,0},r=*rect;int h;DWORD drawFlags=flags&~(DT_CENTER|DT_RIGHT);
                    b->drawW(font,NULL,s+begin,(int)(i-begin),&measure,DT_CALCRECT|DT_SINGLELINE|DT_NOPREFIX,current);
                    r.left=x;h=b->drawW(font,sprite,s+begin,(int)(i-begin),&r,drawFlags,current);if(h>result)result=h;x+=measure.right-measure.left;
                }
                if(tag){if(!shadow)current=(color&0xFF000000)|rgb;i+=7;begin=i+1;}
            }
        }
    }
    HeapFree(GetProcessHeap(),0,plain);if(!(flags&DT_CALCRECT))InterlockedIncrement(&b->rendered);return result;
}
static void callerlog(Backend *b,const void *caller){MEMORY_BASIC_INFORMATION m;char path[MAX_PATH],*name;LONG hit=InterlockedIncrement(&b->hits);if(hit>5)return;
    if(VirtualQuery(caller,&m,sizeof(m))&&m.Type==MEM_IMAGE&&GetModuleFileNameA((HMODULE)m.AllocationBase,path,sizeof(path))){name=strrchr(path,'\\');logline("Thai text backend=%s caller=%s+0x%lX",b->name,name?name+1:path,(unsigned long)((uintptr_t)caller-(uintptr_t)m.AllocationBase));}
    else logline("Thai text backend=%s caller=private-memory",b->name);
}
static BOOL renderbypass(void *self){
    Cached *c;BOOL owned=FALSE;DWORD saved=GetLastError();
    if(g_drawTls==TLS_OUT_OF_INDEXES){SetLastError(saved);return TRUE;}
    if(TlsGetValue(g_drawTls)!=NULL){SetLastError(saved);return TRUE;}
    EnterCriticalSection(&g_lock);
    for(c=g_fonts;c;c=c->next)if(c->thai==self){owned=TRUE;break;}
    LeaveCriticalSection(&g_lock);SetLastError(saved);return owned;
}
static int drawA(Backend *b,void *self,void *sprite,const char *s,INT count,RECT *r,DWORD flags,DWORD color,void *caller){
    WCHAR *decoded=NULL;size_t n=0;int result=-1;InterlockedIncrement(&b->callsA);
    if(renderbypass(self))return b->drawA(self,sprite,s,count,r,flags,color);
    if(s&&r&&count!=0){size_t len=textlenA(s,count),i;if(len<=32768)for(i=0;i+5<=len;i++)if(!memcmp(s+i,"[[th:",5)){decoded=decodeA(s,len,&n);break;}}
    if(!decoded)return b->drawA(self,sprite,s,count,r,flags,color);
    if(!TlsSetValue(g_drawTls,(void*)1)){HeapFree(GetProcessHeap(),0,decoded);return b->drawA(self,sprite,s,count,r,flags,color);}
    callerlog(b,caller);EnterCriticalSection(&g_lock);result=drawthai(b,self,sprite,decoded,n,r,flags,color);LeaveCriticalSection(&g_lock);TlsSetValue(g_drawTls,NULL);HeapFree(GetProcessHeap(),0,decoded);
    if(result<0){InterlockedIncrement(&g_bad_markers);return b->drawA(self,sprite,s,count,r,flags,color);}return result;
}
static int drawW(Backend *b,void *self,void *sprite,const WCHAR *s,INT count,RECT *r,DWORD flags,DWORD color,void *caller){
    WCHAR *decoded=NULL;size_t n=0;int result=-1;InterlockedIncrement(&b->callsW);
    if(renderbypass(self))return b->drawW(self,sprite,s,count,r,flags,color);
    if(s&&r&&count!=0){
        size_t len=textlenW(s,count),i;BOOL hasThai=FALSE;
        if(len<=32768){
            for(i=0;i<len;i++)if((s[i]>=0xE01&&s[i]<=0xE3A)||(s[i]>=0xE3F&&s[i]<=0xE5B)){hasThai=TRUE;break;}
            for(i=0;i+5<=len;i++)if(!wmemcmp(s+i,L"[[th:",5)){decoded=decodeW(s,len,&n);break;}
            if(!decoded&&hasThai){decoded=HeapAlloc(GetProcessHeap(),0,(len+1)*sizeof(WCHAR));if(decoded){memcpy(decoded,s,len*sizeof(WCHAR));decoded[len]=0;n=len;}}
        }
    }
    if(!decoded)return b->drawW(self,sprite,s,count,r,flags,color);
    if(!TlsSetValue(g_drawTls,(void*)1)){HeapFree(GetProcessHeap(),0,decoded);return b->drawW(self,sprite,s,count,r,flags,color);}
    callerlog(b,caller);EnterCriticalSection(&g_lock);result=drawthai(b,self,sprite,decoded,n,r,flags,color);LeaveCriticalSection(&g_lock);TlsSetValue(g_drawTls,NULL);HeapFree(GetProcessHeap(),0,decoded);
    if(result<0){InterlockedIncrement(&g_bad_markers);return b->drawW(self,sprite,s,count,r,flags,color);}return result;
}
static ULONG releasefont(Backend *b,void *self){ULONG refs;Cached **p,*c;EnterCriticalSection(&g_lock);refs=b->release(self);if(!refs){for(p=&g_fonts;(c=*p)!=NULL;p=&c->next)if(c->backend==b&&c->source==self){*p=c->next;b->release(c->thai);HeapFree(GetProcessHeap(),0,c);break;}}LeaveCriticalSection(&g_lock);return refs;}
static HRESULT fontstate(Backend *b,void *self,BOOL reset){Cached *c;HRESULT hr;EnterCriticalSection(&g_lock);c=findfont(b,self);if(!reset&&c)b->lost(c->thai);hr=(reset?b->reset:b->lost)(self);if(reset&&hr>=0&&c)b->reset(c->thai);LeaveCriticalSection(&g_lock);return hr;}
#define WRAPPERS(N) \
static int WINAPI hookA##N(void*a,void*b,const void*c,INT d,RECT*e,DWORD f,DWORD g){return drawA(&g_backends[N],a,b,c,d,e,f,g,__builtin_return_address(0));} \
static int WINAPI hookW##N(void*a,void*b,const void*c,INT d,RECT*e,DWORD f,DWORD g){return drawW(&g_backends[N],a,b,c,d,e,f,g,__builtin_return_address(0));} \
static ULONG WINAPI hookRelease##N(void*p){return releasefont(&g_backends[N],p);} \
static HRESULT WINAPI hookLost##N(void*p){return fontstate(&g_backends[N],p,FALSE);} \
static HRESULT WINAPI hookReset##N(void*p){return fontstate(&g_backends[N],p,TRUE);}
WRAPPERS(0)
WRAPPERS(1)
static void *g_callbacks[2][5]={{hookA0,hookW0,hookRelease0,hookLost0,hookReset0},{hookA1,hookW1,hookRelease1,hookLost1,hookReset1}};
static void *getdevice(BYTE *samp){uintptr_t manager,wrapper,raw;void *device=NULL;manager=read32((uintptr_t)samp+0x26E8E4);if(!manager)return NULL;wrapper=read32(manager);if(!wrapper||read32(wrapper)!=(uintptr_t)samp+0xEA3B8)return NULL;raw=read32(wrapper+4);if(!raw)return NULL;if(((DeviceFn)vt((void*)raw)[3])((void*)raw,&device)<0)return NULL;return device;}
static BOOL initbackend(unsigned index,void *device){
    Backend *b=&g_backends[index];FontDesc desc;void *font=NULL,**v;void *targets[5];void **originals[5];int slots[5]={14,15,2,16,17};unsigned i;MH_STATUS st;
    if(b->ready)return TRUE;
    b->module=GetModuleHandleA(b->name);if(!b->module)return FALSE;
    b->create=(FontCreateFn)(uintptr_t)GetProcAddress(b->module,"D3DXCreateFontIndirectW");if(!b->create)return FALSE;
    memset(&desc,0,sizeof(desc));desc.Height=16;desc.Weight=400;desc.MipLevels=1;desc.CharSet=DEFAULT_CHARSET;wcscpy(desc.FaceName,L"Tahoma");
    if(b->create(device,&desc,&font)<0||!font)return FALSE;
    v=vt(font);
    originals[0]=(void**)&b->drawA;originals[1]=(void**)&b->drawW;originals[2]=(void**)&b->release;originals[3]=(void**)&b->lost;originals[4]=(void**)&b->reset;
    for(i=0;i<5;i++)targets[i]=v[slots[i]];
    for(i=0;i<5;i++){
        st=MH_CreateHook(targets[i],g_callbacks[index][i],originals[i]);
        if(st!=MH_OK){logline("%s hook slot %d failed: %s",b->name,slots[i],MH_StatusToString(st));while(i>0)MH_RemoveHook(targets[--i]);((ReleaseFn)v[2])(font);return FALSE;}
    }
    for(i=0;i<5;i++){st=MH_QueueEnableHook(targets[i]);if(st!=MH_OK)break;}
    if(i<5){for(i=0;i<5;i++){MH_QueueDisableHook(targets[i]);MH_RemoveHook(targets[i]);}((ReleaseFn)v[2])(font);return FALSE;}
    st=MH_ApplyQueued();
    if(st!=MH_OK){logline("%s activation failed: %s",b->name,MH_StatusToString(st));for(i=0;i<5;i++){MH_DisableHook(targets[i]);MH_RemoveHook(targets[i]);}((ReleaseFn)v[2])(font);return FALSE;}
    b->ready=TRUE;b->release(font);logline("Backend installed: %s DrawTextA=%p DrawTextW=%p",b->name,targets[0],targets[1]);return TRUE;
}
static BOOL correctsamp(BYTE *base){IMAGE_DOS_HEADER d;IMAGE_NT_HEADERS32 n;if(!readmem((uintptr_t)base,&d,sizeof(d))||d.e_magic!=IMAGE_DOS_SIGNATURE||d.e_lfanew<0x40||d.e_lfanew>0x1000||!readmem((uintptr_t)base+d.e_lfanew,&n,sizeof(n)))return FALSE;return n.Signature==IMAGE_NT_SIGNATURE&&n.FileHeader.TimeDateStamp==0x5C0B4243&&n.OptionalHeader.AddressOfEntryPoint==0xCC4D0&&n.OptionalHeader.SizeOfImage==0x27E000;}
typedef int (WINAPI *MultiByteFn)(UINT,DWORD,LPCCH,int,LPWSTR,int);
static MultiByteFn g_mbOriginal;
static DWORD g_mbTls=TLS_OUT_OF_INDEXES;
static volatile LONG g_conversionMarkers,g_conversionDecoded;
static int WINAPI mb_hook(UINT cp,DWORD flags,LPCCH input,int bytes,LPWSTR output,int capacity){
    size_t len=0,i,units=0;BOOL marker=FALSE;WCHAR *decoded=NULL;int required;DWORD savedError;
    if(!input||bytes==0||bytes < -1||capacity<0||(!output&&capacity!=0)||(const void*)input==(const void*)output)
        return g_mbOriginal(cp,flags,input,bytes,output,capacity);
    len=bytes==-1?strlen(input):(size_t)bytes;
    if(len>32768)return g_mbOriginal(cp,flags,input,bytes,output,capacity);
    for(i=0;i+5<=len;i++)if(!memcmp(input+i,"[[th:",5)){marker=TRUE;break;}
    if(!marker)return g_mbOriginal(cp,flags,input,bytes,output,capacity);
    savedError=GetLastError();
    if(g_mbTls==TLS_OUT_OF_INDEXES||TlsGetValue(g_mbTls)!=NULL){SetLastError(savedError);return g_mbOriginal(cp,flags,input,bytes,output,capacity);}
    if(!TlsSetValue(g_mbTls,(void*)1)){SetLastError(savedError);return g_mbOriginal(cp,flags,input,bytes,output,capacity);}
    if(InterlockedIncrement(&g_conversionMarkers)<=5){
        const void *caller=__builtin_return_address(0);MEMORY_BASIC_INFORMATION m;char path[MAX_PATH],*name;
        if(VirtualQuery(caller,&m,sizeof(m))&&m.Type==MEM_IMAGE&&GetModuleFileNameA((HMODULE)m.AllocationBase,path,sizeof(path))){name=strrchr(path,'\\');logline("ANSI marker cp=%u flags=%lu caller=%s+0x%lX",cp,flags,name?name+1:path,(unsigned long)((uintptr_t)caller-(uintptr_t)m.AllocationBase));}
        else logline("ANSI marker cp=%u flags=%lu caller=private-memory",cp,flags);
    }
    if((cp==1251||cp==CP_ACP||cp==CP_THREAD_ACP)&&!(flags&~(MB_PRECOMPOSED|MB_ERR_INVALID_CHARS))){
        /* Check the original conversion's validity before replacing marker runs. */
        if(g_mbOriginal(cp,flags,input,bytes,NULL,0)>0)decoded=decodeACodePage(input,len,&units,cp);
    }
    if(!decoded){TlsSetValue(g_mbTls,NULL);SetLastError(savedError);return g_mbOriginal(cp,flags,input,bytes,output,capacity);}
    required=(int)units+(bytes==-1?1:0);
    if(!output||capacity==0){HeapFree(GetProcessHeap(),0,decoded);TlsSetValue(g_mbTls,NULL);SetLastError(savedError);return required;}
    if(capacity<required){HeapFree(GetProcessHeap(),0,decoded);TlsSetValue(g_mbTls,NULL);SetLastError(ERROR_INSUFFICIENT_BUFFER);return 0;}
    memcpy(output,decoded,(size_t)required*sizeof(WCHAR));
    HeapFree(GetProcessHeap(),0,decoded);TlsSetValue(g_mbTls,NULL);SetLastError(savedError);
    InterlockedIncrement(&g_conversionDecoded);return required;
}
static void initconversion(void){
    HMODULE kernel=GetModuleHandleA("kernel32.dll");void *target;MH_STATUS status;
    g_mbTls=TlsAlloc();if(g_mbTls==TLS_OUT_OF_INDEXES){logline("ANSI conversion TLS unavailable");return;}
    target=(void*)(uintptr_t)GetProcAddress(kernel,"MultiByteToWideChar");
    if(!target){logline("ANSI conversion export unavailable");return;}
    status=MH_CreateHook(target,(void*)&mb_hook,(void**)&g_mbOriginal);
    if(status==MH_OK)status=MH_EnableHook(target);
    logline("ANSI conversion hook: %s",MH_StatusToString(status));
}

static DWORD WINAPI worker(void *arg){WCHAR path[MAX_PATH];HMODULE pin;BYTE *samp=NULL;unsigned i;BOOL initialized=FALSE;(void)arg;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,(LPCSTR)&worker,&pin);
    if(!GetModuleFileNameW(g_module,path,MAX_PATH-5))return 1;
    {WCHAR *dot=wcsrchr(path,L'.');if(dot)*dot=0;wcscat(path,L".log");}g_log=_wfopen(path,L"wb");
    if(!g_log){GetTempPathW(MAX_PATH-32,path);wcscat(path,L"ThaiNative.log");g_log=_wfopen(path,L"wb");}
    logline("ThaiNative 2.2-development experimental native font/conversion bridge; no overlay; CP874 markers only");
    for(i=0;i<240;i++){samp=(BYTE*)GetModuleHandleA("samp.dll");if(samp)break;Sleep(250);}
    if(!samp||!correctsamp(samp)){logline("Unsupported or missing SA-MP build; no hooks installed");return 2;}
    if(MH_Initialize()!=MH_OK){logline("MinHook initialization failed");return 3;}
    initconversion();
    g_drawTls=TlsAlloc();
    if(g_drawTls==TLS_OUT_OF_INDEXES){logline("Render TLS unavailable; font hooks disabled");return 4;}
    for(i=0;;i++){
        if(!g_backends[0].ready||!g_backends[1].ready){void *device=getdevice(samp);if(device){unsigned j;for(j=0;j<2;j++)if(!g_backends[j].ready)initbackend(j,device);((ReleaseFn)vt(device)[2])(device);}}
        if(g_backends[0].ready||g_backends[1].ready)initialized=TRUE;
        if(i%40==0){unsigned j;for(j=0;j<2;j++){Backend *b=&g_backends[j];logline("%s ready=%d A=%ld W=%ld hits=%ld drawn=%ld failures=%ld",b->name,b->ready,b->callsA,b->callsW,b->hits,b->rendered,b->failures);}logline("tick=%lu initialized=%d drawFallbacks=%ld conversionMarkers=%ld conversionDecoded=%ld",GetTickCount(),initialized,g_bad_markers,g_conversionMarkers,g_conversionDecoded);}
        Sleep(250);
    }
    return 0;
}
__declspec(dllexport) const char *ThaiNativeVersion(void){return "2.2-development guarded native font bridge";}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,void *reserved){(void)reserved;if(reason==DLL_PROCESS_ATTACH){HANDLE t;g_module=module;InitializeCriticalSection(&g_lock);DisableThreadLibraryCalls(module);t=CreateThread(NULL,0,worker,NULL,0,NULL);if(t)CloseHandle(t);}return TRUE;}
/* Optional native API for integrations; returns 0 when the input has no valid marker. */
__declspec(dllexport) int WINAPI ThaiNativeDecodeA(const char *input,int count,WCHAR *output,int capacity){size_t n=0;WCHAR *text;if(!input||count<0||capacity<0)return 0;text=decodeA(input,(size_t)count,&n);if(!text)return 0;if(output&&capacity>(int)n){memcpy(output,text,(n+1)*sizeof(WCHAR));}else if(output){HeapFree(GetProcessHeap(),0,text);return -(int)(n+1);}HeapFree(GetProcessHeap(),0,text);return (int)n;}
__declspec(dllexport) int WINAPI ThaiNativeDecodeW(const WCHAR *input,int count,WCHAR *output,int capacity){size_t n=0;WCHAR *text;if(!input||count<0||capacity<0)return 0;text=decodeW(input,(size_t)count,&n);if(!text)return 0;if(output&&capacity>(int)n){memcpy(output,text,(n+1)*sizeof(WCHAR));}else if(output){HeapFree(GetProcessHeap(),0,text);return -(int)(n+1);}HeapFree(GetProcessHeap(),0,text);return (int)n;}
