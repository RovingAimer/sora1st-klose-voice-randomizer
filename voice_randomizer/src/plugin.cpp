#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <cstdio>
#include <cstdarg>
#include <vector>
#include <fstream>
#include <filesystem>
#include <memory>
#include <cerrno>
#include <cwctype>
#include "MinHook.h"
#include "core.h"
#include "overlay.h"
#pragma comment(lib, "bcrypt.lib")

namespace {
struct AssetDefinition { const char* name; unsigned id; };
#include "assets.generated.h"
struct Asset { unsigned id; std::string alias, disk; std::wstring wide; std::string source="legacy"; };
std::unordered_map<std::string, Asset> assets;
std::wstring directory, ini_path;
std::string replacement_set="legacy";
std::mutex log_mutex;
// Publish all selection settings and reset state under the same lock used by play_hook.
std::mutex config_mutex;
FILE* log_file = nullptr;
std::unique_ptr<vr::Selector> selector;
std::atomic<bool> enabled{true}, log_cache{true};
std::atomic<vr::Mode> mode{vr::Mode::Random};
std::atomic<unsigned> percent{50};

using Play = uint32_t (__fastcall*)(void*, const vr::PlayRequest*);
using FileOpen = int64_t (__fastcall*)(int64_t, const char*, uint32_t, uint32_t, uint16_t);
using CacheFind = uint32_t (__fastcall*)(void*, const char*);
Play original_play = nullptr;
FileOpen original_open = nullptr;
CacheFind original_cache = nullptr;
using Frame = uint64_t (__fastcall*)(void*, unsigned char);
Frame original_frame = nullptr;

const std::unordered_map<std::string,unsigned>& history_voices() {
    static const auto voices=[] {
        std::unordered_map<std::string,unsigned> result;
        for(const auto& asset:kAssets) if(std::string(asset.name).compare(0,5,"v006_")==0) result.emplace(asset.name,asset.id);
        // Verified against this EXE version's t_voice.tbl; these have no legacy WAV.
        result.emplace("v006_b0100",2011); result.emplace("v006_pv_0001",29019); result.emplace("v006_pv_0002",29020);
        return result;
    }();
    return voices;
}

void log(const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(log_mutex);
    if (!log_file) return;
    SYSTEMTIME st{}; GetLocalTime(&st);
    fprintf(log_file, "%02u:%02u:%02u.%03u tid=%lu ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, GetCurrentThreadId());
    va_list args; va_start(args, fmt); vfprintf(log_file, fmt, args); va_end(args);
    fputc('\n', log_file); fflush(log_file);
}

bool readable_copy(void* dest, const void* source, size_t length) {
    __try { memcpy(dest, source, length); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool read_name(const char* source, char (&out)[260]) {
    if (!source) return false;
    __try {
        for (size_t i=0; i<sizeof(out); ++i) {
            out[i]=source[i]; if (!out[i]) return true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return false;
}

void record_history(uint64_t ticket,unsigned id,const char* name,bool old,vr::Reason reason,uint32_t handle,bool target_old=false) {
    if(!ticket || handle==UINT32_MAX) return;
    const auto badge=vr::ui::badge_for(reason,target_old);
    vr::ui::history().push({ticket,id,name,old,badge});
    log("UI_HISTORY ticket=%llu voice_id=%u name=%s selected=%s badge=%s",ticket,id,name,old?"old":"new",
        badge==vr::ui::Badge::Wai?"wai":badge==vr::ui::Badge::Radiance?"capturing_radiance":"none");
}

uint64_t __fastcall frame_hook(void* render_device,unsigned char sync) {
    IDXGISwapChain* swap=nullptr;
    if(readable_copy(&swap,static_cast<const unsigned char*>(render_device)+0x2d8,sizeof(swap))) vr::ui::draw(swap);
    return original_frame(render_device,sync);
}

uint32_t __fastcall play_hook(void* manager, const vr::PlayRequest* input) {
    vr::PlayRequest copy{};
    char name[260]{};
    if (!readable_copy(&copy, input, sizeof(copy)) || copy.category != 2 || !read_name(copy.name, name))
        return original_play(manager, input);
    const auto known=history_voices().find(name);
    const uint64_t ticket=known!=history_voices().end()?vr::ui::history().ticket():0;
    const unsigned history_id=ticket?known->second:0;
    auto it=assets.find(name);
    vr::Mode selection_mode;
    unsigned selection_percent;
    vr::Selection selection;
    bool participate;
    {
        std::lock_guard<std::mutex> lock(config_mutex);
        selection_mode=mode.load();
        selection_percent=percent.load();
        participate=enabled.load() && it!=assets.end();
        if (participate) selection=selector->choose(name,selection_mode,selection_percent);
    }
    if (!participate) {
        const auto handle=original_play(manager,input);
        record_history(ticket,history_id,name,false,vr::Reason::ForcedNew,handle);
        return handle;
    }
    const Asset& asset=it->second;
    bool use_old=selection.old;
    if (use_old) {
        DWORD attr=GetFileAttributesW(asset.wide.c_str());
        if (attr==INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            log("FALLBACK event=%llu voice_id=%u name=%s reason=old_file_missing", selection.sequence, asset.id, name);
            use_old=false;
        }
    }
    if (use_old) copy.name=asset.alias.c_str();
    uint32_t handle=original_play(manager, &copy);
    if (use_old && handle==UINT32_MAX) {
        log("FALLBACK event=%llu name=%s reason=old_queue_rejected", selection.sequence, name);
        handle=original_play(manager, input);
        use_old=false;
    }
    log("PLAY event=%llu voice_id=%u name=%s selected=%s queued_handle=%u mode=%d source=%s reason=%s roll=%d base_old_percent=%u count_before=%u count_after=%u guarantee_before=%u guarantee_after=%u drawn_selected=%s guarantee_target=%s guarantee_enabled=%u radiance_enabled=%u rules_epoch=%llu",
        selection.sequence, asset.id, name, use_old?"old":"new", handle, static_cast<int>(selection_mode),use_old?asset.source.c_str():"game",
        vr::reason_name(selection.reason),selection.roll,selection_percent,selection.count_before,selection.count_after,
        static_cast<unsigned>(selection.guarantee_before),static_cast<unsigned>(selection.guarantee_after),selection.old?"old":"new",
        selection.rules.target_old?"old":"new",static_cast<unsigned>(selection.rules.guarantee),static_cast<unsigned>(selection.rules.radiance),selection.rules_epoch);
    record_history(ticket,history_id,name,use_old,selection.reason,handle,selection.rules.target_old);
    return handle;
}

int64_t __fastcall open_hook(int64_t self, const char* path, uint32_t a3, uint32_t a4, uint16_t a5) {
    char safe[260]{};
    std::string name;
    bool old=false;
    if (!read_name(path, safe) || !vr::parse_voice_path(safe, name, old) || !old)
        return original_open(self, path, a3, a4, a5);
    auto it=assets.find(name);
    if (it==assets.end()) return original_open(self, path, a3, a4, a5);
    if (it!=assets.end()) {
        DWORD attr=GetFileAttributesW(it->second.wide.c_str());
        if (attr!=INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            auto result=original_open(self, it->second.disk.c_str(), a3, a4, a5);
            log("OPEN name=%s selected=old result=%lld source=%s file=%s", name.c_str(), result,it->second.source.c_str(),it->second.disk.c_str());
            return result;
        }
    }
    // A file may disappear after selection but before the asynchronous load.
    const auto fallback="voice/wav/"+name+".wav";
    log("FALLBACK_OPEN name=%s reason=old_file_missing",name.c_str());
    return original_open(self, fallback.c_str(), a3, a4, a5);
}

uint32_t __fastcall cache_hook(void* cache, const char* path) {
    auto result=original_cache(cache, path);
    if (log_cache.load(std::memory_order_relaxed)) {
        char safe[260]{}; std::string name; bool old=false;
        if (read_name(path, safe) && vr::parse_voice_path(safe, name, old) && assets.find(name)!=assets.end())
            log("CACHE name=%s selected=%s result=%s slot=%u source=%s",name.c_str(),old?"old":"new",result==UINT32_MAX?"miss":"hit",result,
                old?(vr::normalized(safe).find("vr_test_")!=std::string::npos?"placeholder":"legacy"):"game");
    }
    return result;
}

std::string sha256(const std::wstring& path) {
    BCRYPT_ALG_HANDLE alg=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
    DWORD size=0, got=0; std::vector<unsigned char> object;
    std::array<unsigned char,32> digest{};
    if (BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return {};
    bool ok=BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof(size),&got,0)>=0;
    if (ok) {
        object.resize(size);
        ok=BCryptCreateHash(alg,&hash,object.data(),size,nullptr,0,0)>=0;
    }
    std::ifstream file(std::filesystem::path(path),std::ios::binary);
    std::array<char,65536> data{};
    if (!file) ok=false;
    while (ok && file) {
        file.read(data.data(),data.size());
        if (file.gcount()>0) ok=BCryptHashData(hash,reinterpret_cast<PUCHAR>(data.data()),static_cast<ULONG>(file.gcount()),0)>=0;
    }
    if (file.bad()) ok=false;
    if (ok) ok=BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg,0);
    std::string result;
    if (ok) for (auto c:digest) { char pair[3]; sprintf_s(pair,"%02x",c); result+=pair; }
    return result;
}

bool valid_wav(const std::wstring& path) {
    std::ifstream f(std::filesystem::path(path),std::ios::binary|std::ios::ate);
    if (!f) return false;
    auto length=static_cast<uint64_t>(f.tellg());
    if (length<44 || length>16*1024*1024) return false;
    f.seekg(0); std::vector<unsigned char> b(static_cast<size_t>(length));
    if (!f.read(reinterpret_cast<char*>(b.data()),b.size())) return false;
    auto u16=[&](size_t i) { uint16_t v; memcpy(&v,b.data()+i,2); return v; };
    auto u32=[&](size_t i) { uint32_t v; memcpy(&v,b.data()+i,4); return v; };
    if (memcmp(b.data(),"RIFF",4) || memcmp(b.data()+8,"WAVE",4) || uint64_t(u32(4))+8!=length) return false;
    bool format=false, data=false;
    for (size_t at=12; at+8<=b.size();) {
        const size_t size=u32(at+4);
        if (size>b.size()-at-8) return false;
        if (!memcmp(b.data()+at,"fmt ",4))
            format=size>=16 && u16(at+8)==1 && u16(at+10)==1 && u32(at+12)==48000 && u16(at+20)==2 && u16(at+22)==16;
        if (!memcmp(b.data()+at,"data",4)) data=size>0 && size%2==0;
        at+=8+size+(size%2);
    }
    return format && data;
}

int profile_int(const wchar_t* section,const wchar_t* key,int fallback,int low,int high) {
    wchar_t value[64]{};
    GetPrivateProfileStringW(section,key,L"",value,64,ini_path.c_str());
    wchar_t* end=nullptr; errno=0;
    const long parsed=wcstol(value,&end,10);
    if (end==value || errno==ERANGE) return fallback;
    while (iswspace(*end)) ++end;
    if (*end) return fallback;
    return static_cast<int>(std::clamp(parsed,static_cast<long>(low),static_cast<long>(high)));
}

void read_config() {
    wchar_t value[40]{};
    GetPrivateProfileStringW(L"Randomizer",L"Mode",L"random",value,40,ini_path.c_str());
    vr::Mode m=vr::Mode::Random;
    if (!_wcsicmp(value,L"alternate")) m=vr::Mode::Alternate;
    else if (!_wcsicmp(value,L"new")) m=vr::Mode::New;
    else if (!_wcsicmp(value,L"old")) m=vr::Mode::Old;
    unsigned p=profile_int(L"Randomizer",L"OldPercent",50,0,100);
    bool en=profile_int(L"Randomizer",L"Enabled",1,0,1)!=0;
    bool cache=profile_int(L"Randomizer",L"LogCache",0,0,1)!=0;
    vr::Rules rules;
    GetPrivateProfileStringW(L"Randomizer",L"GuaranteeTarget",L"new",value,40,ini_path.c_str());
    rules.target_old=!_wcsicmp(value,L"old");
    rules.guarantee=profile_int(L"Randomizer",L"GuaranteeEnabled",1,0,1)!=0;
    rules.radiance=profile_int(L"Randomizer",L"CapturingRadianceEnabled",1,0,1)!=0;
    static bool first=true;
    static vr::Rules last_rules;
    {
        std::lock_guard<std::mutex> lock(config_mutex);
        if (first || m!=mode.load() || p!=percent.load() || en!=enabled.load() || cache!=log_cache.load() || rules!=last_rules)
            log("CONFIG enabled=%d mode=%d old_percent=%u log_cache=%d guarantee_target=%s guarantee_enabled=%u radiance_enabled=%u rule_state_reset=%d (mode 0=random 1=alternate 2=new 3=old)",
                en,static_cast<int>(m),p,cache,rules.target_old?"old":"new",static_cast<unsigned>(rules.guarantee),static_cast<unsigned>(rules.radiance),rules!=last_rules);
        selector->configure_rules(rules);
        mode.store(m); percent.store(p); enabled.store(en); log_cache.store(cache);
        last_rules=rules; first=false;
    }
    vr::ui::Config ui;
    ui.enabled=profile_int(L"Overlay",L"Enabled",1,0,1)!=0;
    ui.left=profile_int(L"Overlay",L"LeftPercent",2,0,90)/100.f;
    ui.center=profile_int(L"Overlay",L"CenterYPercent",40,0,100)/100.f;
    ui.height=profile_int(L"Overlay",L"HeightPercent",50,10,100)/100.f;
    ui.scroll_ms=static_cast<float>(profile_int(L"Overlay",L"ScrollMs",180,0,2000));
    vr::ui::configure(ui);
}

DWORD WINAPI initialize(void*) {
    wchar_t exe[32768]{}; GetModuleFileNameW(nullptr,exe,32768);
    std::filesystem::path exe_path(exe);
    if (_wcsicmp(exe_path.filename().c_str(),L"sora_1st.exe")) return 0;
    directory=(exe_path.parent_path()/L"voice_randomizer").wstring();
    ini_path=directory+L"\\voice_randomizer.ini";
    _wfopen_s(&log_file,(directory+L"\\voice_randomizer.log").c_str(),L"w");
    log("INIT Voice Randomizer 1.1.0; scope=all matched 1st voice assets; no PAC modification");
    vr::ui::initialize(directory,log);
    log("RULES radiance_threshold=3 state_scope=all_eligible_voices initial_count=0 initial_guarantee=0; target/switch changes reset both states; debug modes pause state; see CONFIG/PLAY for active rules");
    auto hash=sha256(exe);
    if (hash!="9585d3d993874d65156aba633c00b8a25faab3afde8953a8a183c80fb8f657ac") {
        log("DISABLED unsupported executable sha256=%s; no hooks installed",hash.c_str()); return 0;
    }
    uint64_t seed=GetTickCount64() ^ GetCurrentProcessId();
    BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&seed),sizeof(seed),BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    selector=std::make_unique<vr::Selector>(seed);
    wchar_t set_name[32]{};
    GetPrivateProfileStringW(L"Randomizer",L"ReplacementSet",L"legacy",set_name,32,ini_path.c_str());
    if (!_wcsicmp(set_name,L"placeholder")) replacement_set="placeholder";
    log("ASSET_SET source=%s (takes effect at game startup)",replacement_set.c_str());
    for (const auto& def:kAssets) {
        std::wstring wide=directory+(replacement_set=="placeholder"?L"\\assets\\placeholder\\":L"\\assets\\old\\");
        for (const char* p=def.name; *p; ++p) wide+=static_cast<wchar_t>(*p);
        wide+=L".wav";
        if (!valid_wav(wide)) { log("SKIP name=%s reason=invalid_or_missing_wav",def.name); continue; }
        char narrow[32768]{}; BOOL substituted=FALSE;
        int count=WideCharToMultiByte(CP_ACP,WC_NO_BEST_FIT_CHARS,wide.c_str(),-1,narrow,sizeof(narrow),nullptr,&substituted);
        if (!count || substituted || strlen(narrow)>=260) { log("SKIP name=%s reason=unsupported_path",def.name); continue; }
        assets.emplace(def.name,Asset{def.id,(replacement_set=="placeholder"?"vr_test_":"vr_old_")+std::string(def.name),narrow,wide,replacement_set});
    }
    if (assets.empty()) { log("DISABLED no valid replacement WAVs"); return 0; }
    read_config();
    const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    void* play=reinterpret_cast<void*>(base+0x4a1bf0);
    void* open=reinterpret_cast<void*>(base+0x52ee30);
    void* cache=reinterpret_cast<void*>(base+0x57d1c0);
    MH_STATUS status=MH_Initialize();
    if (status!=MH_OK) { log("DISABLED MinHook initialization failed=%d",status); return 0; }
    status=MH_CreateHook(play,reinterpret_cast<void*>(play_hook),reinterpret_cast<void**>(&original_play));
    if (status==MH_OK) status=MH_CreateHook(open,reinterpret_cast<void*>(open_hook),reinterpret_cast<void**>(&original_open));
    if (status==MH_OK) status=MH_CreateHook(cache,reinterpret_cast<void*>(cache_hook),reinterpret_cast<void**>(&original_cache));
    if (status!=MH_OK) { log("DISABLED hook creation failed=%d",status); MH_Uninitialize(); return 0; }
    status=MH_EnableHook(MH_ALL_HOOKS);
    if (status!=MH_OK) { log("DISABLED enabling hooks failed=%d",status); MH_DisableHook(MH_ALL_HOOKS); MH_Uninitialize(); return 0; }
    void* frame=reinterpret_cast<void*>(base+0x51d570);
    status=MH_CreateHook(frame,reinterpret_cast<void*>(frame_hook),reinterpret_cast<void**>(&original_frame));
    if(status==MH_OK) status=MH_EnableHook(frame);
    if(status==MH_OK) log("UI_HOOKED frame_rva=51d570 voices=%zu images=4 history=10 newest=bottom",history_voices().size());
    else log("UI_DISABLED hook_status=%d; voice hooks remain active",status);
    HMODULE pinned=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&initialize),&pinned);
    log("READY assets=%zu play_rva=4a1bf0 open_rva=52ee30 cache_rva=57d1c0; ui_history=10",assets.size());
    for (;;) { Sleep(1000); read_config(); }
}
} // namespace

BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if (reason==DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        HANDLE thread=CreateThread(nullptr,0,initialize,nullptr,0,nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
