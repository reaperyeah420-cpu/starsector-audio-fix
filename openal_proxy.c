#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <initguid.h>
#include <mmdeviceapi.h>

typedef void          ALCdevice;
typedef void          ALCcontext;
typedef char          ALCchar;
typedef int           ALCint;
typedef unsigned int  ALCuint;
typedef int           ALCenum;
typedef int           ALCsizei;
typedef unsigned char ALCboolean;
typedef void          ALCvoid;

typedef char          ALboolean;
typedef char          ALchar;
typedef int           ALint;
typedef unsigned int  ALuint;
typedef int           ALsizei;
typedef int           ALenum;
typedef float         ALfloat;
typedef double        ALdouble;
typedef void          ALvoid;
typedef short         ALshort;
typedef unsigned char ALubyte;
typedef long long     ALint64SOFT;

#define EXPORT __declspec(dllexport)
#define CC     __cdecl

static HMODULE       g_real    = NULL;
static volatile LONG g_running = 0;

#define ALC_CONNECTED 0x313

typedef ALCcontext*    (CC *pfnGetCtx_t)(void);
typedef ALCdevice*     (CC *pfnGetDev_t)(ALCcontext*);
typedef void           (CC *pfnGetIntv_t)(ALCdevice*, ALCenum, ALCsizei, ALCint*);
typedef ALCboolean     (CC *pfnReopen_t)(ALCdevice*, const ALCchar*, const ALCint*);
typedef void*          (CC *pfnGetProc_t)(ALCdevice*, const ALCchar*);

static void GetWinDefaultDeviceId(WCHAR *out, int maxLen) {
    IMMDeviceEnumerator *pEnum = NULL;
    IMMDevice           *pDev  = NULL;
    WCHAR               *pwId  = NULL;
    out[0] = L'\0';
    if (FAILED(CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                                 &IID_IMMDeviceEnumerator, (void**)&pEnum)))
        return;
    if (SUCCEEDED(pEnum->lpVtbl->GetDefaultAudioEndpoint(pEnum, eRender, eConsole, &pDev))) {
        if (SUCCEEDED(pDev->lpVtbl->GetId(pDev, &pwId)) && pwId) {
            wcsncpy(out, pwId, maxLen - 1);
            out[maxLen - 1] = L'\0';
            CoTaskMemFree(pwId);
        }
        pDev->lpVtbl->Release(pDev);
    }
    pEnum->lpVtbl->Release(pEnum);
}

static DWORD WINAPI AudioMonitorThread(LPVOID param) {
    pfnGetCtx_t  fnGetCtx  = (pfnGetCtx_t) GetProcAddress(g_real, "alcGetCurrentContext");
    pfnGetDev_t  fnGetDev  = (pfnGetDev_t) GetProcAddress(g_real, "alcGetContextsDevice");
    pfnGetIntv_t fnGetIntv = (pfnGetIntv_t)GetProcAddress(g_real, "alcGetIntegerv");
    pfnGetProc_t fnGetProc = (pfnGetProc_t)GetProcAddress(g_real, "alcGetProcAddress");

    pfnReopen_t fnReopen = NULL;

    if (!fnGetCtx || !fnGetDev || !fnGetIntv || !fnGetProc) return 0;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    #define MAX_DEV_ID    256
    #define CHANGE_DEBOUNCE 3
    WCHAR knownDefaultId[MAX_DEV_ID] = {0};
    int   changeSecs = 0;

    while (InterlockedCompareExchange(&g_running, 0, 0)) {
        Sleep(1000);

        ALCcontext* ctx = fnGetCtx();
        if (!ctx) continue;
        ALCdevice* dev = fnGetDev(ctx);
        if (!dev) continue;

        if (!fnReopen) {
            fnReopen = (pfnReopen_t)fnGetProc(dev, "alcReopenDeviceSOFT");
            if (!fnReopen) continue;
        }

        WCHAR curId[MAX_DEV_ID];
        GetWinDefaultDeviceId(curId, MAX_DEV_ID);
        if (curId[0] == L'\0') continue;

        if (knownDefaultId[0] == L'\0')
            wcsncpy(knownDefaultId, curId, MAX_DEV_ID - 1);

        ALCint connected = 1;
        fnGetIntv(dev, (ALCenum)ALC_CONNECTED, 1, &connected);
        if (!connected) {
            fnReopen(dev, NULL, NULL);
            GetWinDefaultDeviceId(knownDefaultId, MAX_DEV_ID);
            changeSecs = 0;
            continue;
        }

        if (wcscmp(curId, knownDefaultId) != 0) {
            changeSecs++;
            if (changeSecs >= CHANGE_DEBOUNCE) {
                ALCboolean ok = fnReopen(dev, NULL, NULL);
                if (ok) {
                    wcsncpy(knownDefaultId, curId, MAX_DEV_ID - 1);
                    changeSecs = 0;
                }
            }
        } else {
            changeSecs = 0;
        }
    }

    CoUninitialize();
    return 0;
}

static HMODULE GetOwnModule(void) {
    HMODULE h = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                        (LPCWSTR)(void*)&GetOwnModule, &h);
    return h;
}

static void EnsureRealLoaded(void) {
    if (g_real) return;
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(GetOwnModule(), path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) wcscpy(slash + 1, L"OpenAL64_soft.dll");
    g_real = LoadLibraryW(path);
    if (g_real && InterlockedCompareExchange(&g_running, 1, 0) == 0) {
        HANDLE t = CreateThread(NULL, 0, AudioMonitorThread, NULL, 0, NULL);
        if (t) CloseHandle(t);
    }
}

static void* getfn(const char* name) {
    EnsureRealLoaded();
    if (!g_real) return NULL;
    return (void*)GetProcAddress(g_real, name);
}

BOOL WINAPI DllMain(HINSTANCE hDll, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_DETACH) {
        InterlockedExchange(&g_running, 0);
        if (g_real) { FreeLibrary(g_real); g_real = NULL; }
    }
    return TRUE;
}

EXPORT ALCdevice* CC alcOpenDevice(const ALCchar* devicename) {
    typedef ALCdevice* (CC *fn_t)(const ALCchar*);
    static fn_t fn = NULL;
    if (!fn) fn = (fn_t)getfn("alcOpenDevice");
    return fn ? fn(NULL) : NULL;
}

#define FWD0(ret, name) \
    EXPORT ret CC name(void) { \
        typedef ret (CC *fn_t)(void); \
        static fn_t fn = NULL; if (!fn) fn = (fn_t)getfn(#name); \
        return fn ? fn() : (ret)0; }

#define FWD1(ret, name, T1, a1) \
    EXPORT ret CC name(T1 a1) { \
        typedef ret (CC *fn_t)(T1); \
        static fn_t fn = NULL; if (!fn) fn = (fn_t)getfn(#name); \
        return fn ? fn(a1) : (ret)0; }

#define FWD2(ret, name, T1,a1, T2,a2) \
    EXPORT ret CC name(T1 a1, T2 a2) { \
        typedef ret (CC *fn_t)(T1,T2); \
        static fn_t fn = NULL; if (!fn) fn = (fn_t)getfn(#name); \
        return fn ? fn(a1,a2) : (ret)0; }

#define FWD3(ret, name, T1,a1, T2,a2, T3,a3) \
    EXPORT ret CC name(T1 a1, T2 a2, T3 a3) { \
        typedef ret (CC *fn_t)(T1,T2,T3); \
        static fn_t fn = NULL; if (!fn) fn = (fn_t)getfn(#name); \
        return fn ? fn(a1,a2,a3) : (ret)0; }

#define FWD4(ret, name, T1,a1, T2,a2, T3,a3, T4,a4) \
    EXPORT ret CC name(T1 a1, T2 a2, T3 a3, T4 a4) { \
        typedef ret (CC *fn_t)(T1,T2,T3,T4); \
        static fn_t fn = NULL; if (!fn) fn = (fn_t)getfn(#name); \
        return fn ? fn(a1,a2,a3,a4) : (ret)0; }

#define VFWD0(name) \
    EXPORT void CC name(void) { \
        typedef void (CC *fn_t)(void); \
        static fn_t fn = NULL; if (!fn) fn = (fn_t)getfn(#name); \
        if (fn) fn(); }

#define VFWD1(name, T1,a1) \
    EXPORT void CC name(T1 a1) { \
        typedef void (CC *fn_t)(T1); \
        static fn_t fn = NULL; if (!fn) fn = (fn_t)getfn(#name); \
        if (fn) fn(a1); }

#define VFWD2(name, T1,a1, T2,a2) \
    EXPORT void CC name(T1 a1, T2 a2) { \
        typedef void (CC *fn_t)(T1,T2); \
        static fn_t fn = NULL; if (!fn) fn = (fn_t)getfn(#name); \
        if (fn) fn(a1,a2); }

#define VFWD3(name, T1,a1, T2,a2, T3,a3) \
    EXPORT void CC name(T1 a1, T2 a2, T3 a3) { \
        typedef void (CC *fn_t)(T1,T2,T3); \
        static fn_t fn = NULL; if (!fn) fn = (fn_t)getfn(#name); \
        if (fn) fn(a1,a2,a3); }

#define VFWD4(name, T1,a1, T2,a2, T3,a3, T4,a4) \
    EXPORT void CC name(T1 a1, T2 a2, T3 a3, T4 a4) { \
        typedef void (CC *fn_t)(T1,T2,T3,T4); \
        static fn_t fn = NULL; if (!fn) fn = (fn_t)getfn(#name); \
        if (fn) fn(a1,a2,a3,a4); }

#define VFWD5(name, T1,a1, T2,a2, T3,a3, T4,a4, T5,a5) \
    EXPORT void CC name(T1 a1, T2 a2, T3 a3, T4 a4, T5 a5) { \
        typedef void (CC *fn_t)(T1,T2,T3,T4,T5); \
        static fn_t fn = NULL; if (!fn) fn = (fn_t)getfn(#name); \
        if (fn) fn(a1,a2,a3,a4,a5); }

#define VFWD6(name, T1,a1, T2,a2, T3,a3, T4,a4, T5,a5, T6,a6) \
    EXPORT void CC name(T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6) { \
        typedef void (CC *fn_t)(T1,T2,T3,T4,T5,T6); \
        static fn_t fn = NULL; if (!fn) fn = (fn_t)getfn(#name); \
        if (fn) fn(a1,a2,a3,a4,a5,a6); }

FWD2(ALCcontext*, alcCreateContext,     ALCdevice*,d, const ALCint*,a)
FWD1(ALCboolean,  alcMakeContextCurrent, ALCcontext*,c)
VFWD1(alcProcessContext,  ALCcontext*,c)
VFWD1(alcSuspendContext,  ALCcontext*,c)
VFWD1(alcDestroyContext,  ALCcontext*,c)
FWD0 (ALCcontext*, alcGetCurrentContext)
FWD1 (ALCdevice*,  alcGetContextsDevice, ALCcontext*,c)
FWD1 (ALCboolean,  alcCloseDevice,       ALCdevice*,d)
FWD1 (ALCenum,     alcGetError,          ALCdevice*,d)
FWD2 (ALCboolean,  alcIsExtensionPresent, ALCdevice*,d, const ALCchar*,e)
FWD2 (void*,       alcGetProcAddress,    ALCdevice*,d, const ALCchar*,n)
FWD2 (ALCenum,     alcGetEnumValue,      ALCdevice*,d, const ALCchar*,n)
FWD2 (const ALCchar*, alcGetString,      ALCdevice*,d, ALCenum,p)
VFWD4(alcGetIntegerv, ALCdevice*,d, ALCenum,p, ALCsizei,s, ALCint*,v)

FWD4 (ALCdevice*, alcCaptureOpenDevice, const ALCchar*,n, ALCuint,f, ALCenum,fmt, ALCsizei,b)
FWD1 (ALCboolean, alcCaptureCloseDevice, ALCdevice*,d)
VFWD1(alcCaptureStart,   ALCdevice*,d)
VFWD1(alcCaptureStop,    ALCdevice*,d)
VFWD3(alcCaptureSamples, ALCdevice*,d, ALCvoid*,b, ALCsizei,s)

VFWD1(alEnable,  ALenum,c)
VFWD1(alDisable, ALenum,c)
FWD1 (ALboolean, alIsEnabled, ALenum,c)

FWD1 (const ALchar*, alGetString,    ALenum,p)
VFWD2(alGetBooleanv, ALenum,p, ALboolean*,v)
VFWD2(alGetIntegerv, ALenum,p, ALint*,v)
VFWD2(alGetFloatv,   ALenum,p, ALfloat*,v)
VFWD2(alGetDoublev,  ALenum,p, ALdouble*,v)
FWD1 (ALboolean, alGetBoolean, ALenum,p)
FWD1 (ALint,     alGetInteger, ALenum,p)
FWD1 (ALfloat,   alGetFloat,   ALenum,p)
FWD1 (ALdouble,  alGetDouble,  ALenum,p)
FWD0 (ALenum,    alGetError)
FWD1 (ALboolean, alIsExtensionPresent, const ALchar*,e)
FWD1 (void*,     alGetProcAddress,     const ALchar*,n)
FWD1 (ALenum,    alGetEnumValue,       const ALchar*,n)

VFWD2(alListenerf,  ALenum,p, ALfloat,v)
VFWD4(alListener3f, ALenum,p, ALfloat,v1, ALfloat,v2, ALfloat,v3)
VFWD2(alListenerfv, ALenum,p, const ALfloat*,v)
VFWD2(alListeneri,  ALenum,p, ALint,v)
VFWD4(alListener3i, ALenum,p, ALint,v1, ALint,v2, ALint,v3)
VFWD2(alListeneriv, ALenum,p, const ALint*,v)
VFWD2(alGetListenerf,  ALenum,p, ALfloat*,v)
VFWD4(alGetListener3f, ALenum,p, ALfloat*,v1, ALfloat*,v2, ALfloat*,v3)
VFWD2(alGetListenerfv, ALenum,p, ALfloat*,v)
VFWD2(alGetListeneri,  ALenum,p, ALint*,v)
VFWD4(alGetListener3i, ALenum,p, ALint*,v1, ALint*,v2, ALint*,v3)
VFWD2(alGetListeneriv, ALenum,p, ALint*,v)

VFWD2(alGenSources,    ALsizei,n, ALuint*,s)
VFWD2(alDeleteSources, ALsizei,n, const ALuint*,s)
FWD1 (ALboolean, alIsSource, ALuint,s)
VFWD3(alSourcef,  ALuint,s, ALenum,p, ALfloat,v)
VFWD5(alSource3f, ALuint,s, ALenum,p, ALfloat,v1, ALfloat,v2, ALfloat,v3)
VFWD3(alSourcefv, ALuint,s, ALenum,p, const ALfloat*,v)
VFWD3(alSourcei,  ALuint,s, ALenum,p, ALint,v)
VFWD5(alSource3i, ALuint,s, ALenum,p, ALint,v1, ALint,v2, ALint,v3)
VFWD3(alSourceiv, ALuint,s, ALenum,p, const ALint*,v)
VFWD3(alGetSourcef,  ALuint,s, ALenum,p, ALfloat*,v)
VFWD3(alGetSourcefv, ALuint,s, ALenum,p, ALfloat*,v)
VFWD3(alGetSourcei,  ALuint,s, ALenum,p, ALint*,v)
VFWD3(alGetSourceiv, ALuint,s, ALenum,p, ALint*,v)
VFWD2(alSourcePlayv,  ALsizei,n, const ALuint*,s)
VFWD2(alSourceStopv,  ALsizei,n, const ALuint*,s)
VFWD2(alSourceRewindv,ALsizei,n, const ALuint*,s)
VFWD2(alSourcePausev, ALsizei,n, const ALuint*,s)
VFWD1(alSourcePlay,   ALuint,s)
VFWD1(alSourceStop,   ALuint,s)
VFWD1(alSourceRewind, ALuint,s)
VFWD1(alSourcePause,  ALuint,s)
VFWD3(alSourceQueueBuffers,   ALuint,s, ALsizei,n, const ALuint*,b)
VFWD3(alSourceUnqueueBuffers, ALuint,s, ALsizei,n, ALuint*,b)

VFWD2(alGenBuffers,    ALsizei,n, ALuint*,b)
VFWD2(alDeleteBuffers, ALsizei,n, const ALuint*,b)
FWD1 (ALboolean, alIsBuffer, ALuint,b)
VFWD3(alBufferf,  ALuint,b, ALenum,p, ALfloat,v)
VFWD5(alBuffer3f, ALuint,b, ALenum,p, ALfloat,v1, ALfloat,v2, ALfloat,v3)
VFWD3(alBufferfv, ALuint,b, ALenum,p, const ALfloat*,v)
VFWD3(alBufferi,  ALuint,b, ALenum,p, ALint,v)
VFWD5(alBuffer3i, ALuint,b, ALenum,p, ALint,v1, ALint,v2, ALint,v3)
VFWD3(alBufferiv, ALuint,b, ALenum,p, const ALint*,v)
VFWD3(alGetBufferf,  ALuint,b, ALenum,p, ALfloat*,v)
VFWD3(alGetBufferfv, ALuint,b, ALenum,p, ALfloat*,v)
VFWD3(alGetBufferi,  ALuint,b, ALenum,p, ALint*,v)
VFWD3(alGetBufferiv, ALuint,b, ALenum,p, ALint*,v)

EXPORT void CC alGetBuffer3f(ALuint b, ALenum p, ALfloat* v1, ALfloat* v2, ALfloat* v3) {
    typedef void (CC *fn_t)(ALuint,ALenum,ALfloat*,ALfloat*,ALfloat*);
    static fn_t fn = NULL; if (!fn) fn = (fn_t)getfn("alGetBuffer3f");
    if (fn) fn(b,p,v1,v2,v3);
}
EXPORT void CC alGetBuffer3i(ALuint b, ALenum p, ALint* v1, ALint* v2, ALint* v3) {
    typedef void (CC *fn_t)(ALuint,ALenum,ALint*,ALint*,ALint*);
    static fn_t fn = NULL; if (!fn) fn = (fn_t)getfn("alGetBuffer3i");
    if (fn) fn(b,p,v1,v2,v3);
}
EXPORT void CC alGetSource3f(ALuint s, ALenum p, ALfloat* v1, ALfloat* v2, ALfloat* v3) {
    typedef void (CC *fn_t)(ALuint,ALenum,ALfloat*,ALfloat*,ALfloat*);
    static fn_t fn = NULL; if (!fn) fn = (fn_t)getfn("alGetSource3f");
    if (fn) fn(s,p,v1,v2,v3);
}
EXPORT void CC alGetSource3i(ALuint s, ALenum p, ALint* v1, ALint* v2, ALint* v3) {
    typedef void (CC *fn_t)(ALuint,ALenum,ALint*,ALint*,ALint*);
    static fn_t fn = NULL; if (!fn) fn = (fn_t)getfn("alGetSource3i");
    if (fn) fn(s,p,v1,v2,v3);
}

VFWD1(alDopplerFactor,  ALfloat,v)
VFWD1(alDopplerVelocity,ALfloat,v)
VFWD1(alSpeedOfSound,   ALfloat,v)
VFWD1(alDistanceModel,  ALenum,m)

EXPORT void CC alBufferData(ALuint b, ALenum fmt, const ALvoid* data, ALsizei size, ALsizei freq) {
    typedef void (CC *fn_t)(ALuint,ALenum,const ALvoid*,ALsizei,ALsizei);
    static fn_t fn = NULL; if (!fn) fn = (fn_t)getfn("alBufferData");
    if (fn) fn(b,fmt,data,size,freq);
}

VFWD2(alGenFilters,    ALsizei,n, ALuint*,filters)
VFWD2(alDeleteFilters, ALsizei,n, const ALuint*,filters)
FWD1 (ALboolean, alIsFilter, ALuint,filter)
VFWD3(alFilteri,     ALuint,filter, ALenum,param, ALint,v)
VFWD3(alFilteriv,    ALuint,filter, ALenum,param, const ALint*,v)
VFWD3(alFilterf,     ALuint,filter, ALenum,param, ALfloat,v)
VFWD3(alFilterfv,    ALuint,filter, ALenum,param, const ALfloat*,v)
VFWD3(alGetFilteri,  ALuint,filter, ALenum,param, ALint*,v)
VFWD3(alGetFilteriv, ALuint,filter, ALenum,param, ALint*,v)
VFWD3(alGetFilterf,  ALuint,filter, ALenum,param, ALfloat*,v)
VFWD3(alGetFilterfv, ALuint,filter, ALenum,param, ALfloat*,v)

VFWD2(alGenEffects,    ALsizei,n, ALuint*,effects)
VFWD2(alDeleteEffects, ALsizei,n, const ALuint*,effects)
FWD1 (ALboolean, alIsEffect, ALuint,effect)
VFWD3(alEffecti,     ALuint,effect, ALenum,param, ALint,v)
VFWD3(alEffectiv,    ALuint,effect, ALenum,param, const ALint*,v)
VFWD3(alEffectf,     ALuint,effect, ALenum,param, ALfloat,v)
VFWD3(alEffectfv,    ALuint,effect, ALenum,param, const ALfloat*,v)
VFWD3(alGetEffecti,  ALuint,effect, ALenum,param, ALint*,v)
VFWD3(alGetEffectiv, ALuint,effect, ALenum,param, ALint*,v)
VFWD3(alGetEffectf,  ALuint,effect, ALenum,param, ALfloat*,v)
VFWD3(alGetEffectfv, ALuint,effect, ALenum,param, ALfloat*,v)

VFWD2(alGenAuxiliaryEffectSlots,    ALsizei,n, ALuint*,slots)
VFWD2(alDeleteAuxiliaryEffectSlots, ALsizei,n, const ALuint*,slots)
FWD1 (ALboolean, alIsAuxiliaryEffectSlot, ALuint,slot)
VFWD3(alAuxiliaryEffectSloti,     ALuint,slot, ALenum,param, ALint,v)
VFWD3(alAuxiliaryEffectSlotiv,    ALuint,slot, ALenum,param, const ALint*,v)
VFWD3(alAuxiliaryEffectSlotf,     ALuint,slot, ALenum,param, ALfloat,v)
VFWD3(alAuxiliaryEffectSlotfv,    ALuint,slot, ALenum,param, const ALfloat*,v)
VFWD3(alGetAuxiliaryEffectSloti,  ALuint,slot, ALenum,param, ALint*,v)
VFWD3(alGetAuxiliaryEffectSlotiv, ALuint,slot, ALenum,param, ALint*,v)
VFWD3(alGetAuxiliaryEffectSlotf,  ALuint,slot, ALenum,param, ALfloat*,v)
VFWD3(alGetAuxiliaryEffectSlotfv, ALuint,slot, ALenum,param, ALfloat*,v)

VFWD3(alSourcedSOFT,  ALuint,s, ALenum,p, ALdouble,v)
VFWD3(alSourcedvSOFT, ALuint,s, ALenum,p, const ALdouble*,v)
VFWD5(alSource3dSOFT, ALuint,s, ALenum,p, ALdouble,v1, ALdouble,v2, ALdouble,v3)
VFWD3(alGetSourcedSOFT,  ALuint,s, ALenum,p, ALdouble*,v)
VFWD3(alGetSourcedvSOFT, ALuint,s, ALenum,p, ALdouble*,v)
VFWD5(alGetSource3dSOFT, ALuint,s, ALenum,p, ALdouble*,v1, ALdouble*,v2, ALdouble*,v3)
VFWD3(alSourcei64SOFT,  ALuint,s, ALenum,p, ALint64SOFT,v)
VFWD3(alSourcei64vSOFT, ALuint,s, ALenum,p, const ALint64SOFT*,v)
VFWD5(alSource3i64SOFT, ALuint,s, ALenum,p, ALint64SOFT,v1, ALint64SOFT,v2, ALint64SOFT,v3)
VFWD3(alGetSourcei64SOFT,  ALuint,s, ALenum,p, ALint64SOFT*,v)
VFWD3(alGetSourcei64vSOFT, ALuint,s, ALenum,p, ALint64SOFT*,v)
VFWD5(alGetSource3i64SOFT, ALuint,s, ALenum,p, ALint64SOFT*,v1, ALint64SOFT*,v2, ALint64SOFT*,v3)

FWD1 (ALboolean, alIsBufferFormatSupportedSOFT, ALenum,format)
VFWD0(alDeferUpdatesSOFT)
VFWD0(alProcessUpdatesSOFT)
VFWD5(alBufferSubDataSOFT, ALuint,b, ALenum,fmt, const ALvoid*,data, ALsizei,offset, ALsizei,length)
VFWD6(alBufferSubSamplesSOFT, ALuint,b, ALsizei,offset, ALsizei,samples, ALenum,channels, ALenum,type, const ALvoid*,data)
VFWD6(alGetBufferSamplesSOFT, ALuint,b, ALsizei,offset, ALsizei,samples, ALenum,channels, ALenum,type, ALvoid*,data)
EXPORT void CC alBufferSamplesSOFT(ALuint buffer, ALuint samplerate, ALenum internalformat,
                                    ALsizei samples, ALenum channels, ALenum type, const ALvoid* data) {
    typedef void (CC *fn_t)(ALuint,ALuint,ALenum,ALsizei,ALenum,ALenum,const ALvoid*);
    static fn_t fn = NULL; if (!fn) fn = (fn_t)getfn("alBufferSamplesSOFT");
    if (fn) fn(buffer, samplerate, internalformat, samples, channels, type, data);
}

FWD3 (ALCdevice*, alcLoopbackOpenDeviceSOFT, const ALCchar*,n, ALCuint,freq, ALCenum,fmt)
FWD4 (ALCboolean, alcIsRenderFormatSupportedSOFT, ALCdevice*,d, ALCsizei,freq, ALCenum,channels, ALCenum,type)
VFWD3(alcRenderSamplesSOFT, ALCdevice*,d, ALCvoid*,buffer, ALCsizei,samples)
FWD0 (ALCcontext*, alcGetThreadContext)
FWD1 (ALCboolean,  alcSetThreadContext, ALCcontext*,c)
