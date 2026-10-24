#include "target_patterns.h"
// Reconstructed third-party behavior; provenance and license boundary: VoiceRandomizer.h.
#include "VoiceRandomizer.h"
#include "detours.h"
#include <bcrypt.h>
#include <xaudio2.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cwctype>
#include <memory>
#include <string_view>

namespace voice {
namespace {
// Only the observed fields are typed. The remaining request is copied byte-for-byte.
struct Request {
    uint32_t kind;
    uint32_t reserved;
    const char* name;
    std::array<std::byte, 0xC0> remainder;
};
static_assert(sizeof(Request) == 0xD0 && offsetof(Request, name) == 8);
using Play = uint32_t (__fastcall*)(void*, const Request*);
using Cache = int32_t (__fastcall*)(void*, const char*);
// Original 51D570 ends with xor eax,eax / ret. Preserve its return value.
using Frame = uint32_t (__fastcall*)(void*, uint8_t);
using CacheKey = void (__fastcall*)(char*, const char*);
// Native sampler creation, validated at RVA 586AE0. All stack arguments are
// preserved; channel controls are applied before the newly created voice starts.
using Sampler = bool (__fastcall*)(void*, uint32_t, const char*, uint32_t,
    uint32_t, uint32_t, const void*, uint32_t, const WAVEFORMATEX*, const XAUDIO2_BUFFER*);
Play originalPlay = nullptr;
Cache originalCache = nullptr;
Frame originalFrame = nullptr;
CacheKey originalCacheKey = nullptr;
Sampler originalSampler = nullptr;
using ProcessTask = uint32_t (__fastcall*)(void*, const void*);
using InsertWave = uint32_t (__fastcall*)(void*, const char*, const void*, uint64_t, void*);
using NativeMutex = int (__fastcall*)(void*);
ProcessTask originalProcessTask = nullptr;
InsertWave insertWave = nullptr;
NativeMutex lockAudioCache = nullptr, unlockAudioCache = nullptr;
struct StereoContext {
    bool right = false, created = false;
    float gain = 1.f;
    bool attempted = false;
    void* sampler = nullptr;
    uint32_t slot = UINT32_MAX;
    IXAudio2SourceVoice* source = nullptr;
    IXAudio2SubmixVoice* destination = nullptr;
};
thread_local StereoContext* currentStereo = nullptr;
thread_local float defaultVoiceGain = 1.f;
struct DefaultVolumeScope {
    float previous;
    explicit DefaultVolumeScope(float gain) : previous(defaultVoiceGain) { defaultVoiceGain=gain; }
    ~DefaultVolumeScope() { defaultVoiceGain=previous; }
};
struct StereoScope {
    StereoContext* previous;
    explicit StereoScope(StereoContext& context) : previous(currentStereo) { currentStereo=&context; }
    ~StereoScope() { currentStereo=previous; }
};
struct State {
    explicit State(uint64_t seed) : rules(seed) {}
    std::wstring root, ini;
    std::unordered_map<std::string, Resource> resources;
    std::unordered_map<std::string, uint32_t> uiIndex;
    std::atomic<uint64_t> tickets{0};
    std::mutex configMutex;
    Config config;
    Rules rules;
};
State* state = nullptr; // Process lifetime: hooks, aliases and worker stay mapped.
volatile LONG initialization = 0;
DWORD initializationStatus = ERROR_GEN_FAILURE;
std::mutex logMutex;
std::wstring logPath;

bool SafeCopy(void* destination, const void* source, size_t size) noexcept {
    __try { memcpy(destination, source, size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool SafeName(char* destination, const char* source) noexcept {
    __try {
        if (!source) return false;
        for (size_t i = 0; i < MAX_PATH; ++i) {
            destination[i] = source[i];
            if (!source[i]) return true;
        }
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool FileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}
std::wstring Setting(const wchar_t* section, const wchar_t* key, const wchar_t* fallback, DWORD capacity = 40) {
    wchar_t value[64]{};
    GetPrivateProfileStringW(section, key, fallback, value, capacity, state->ini.c_str());
    return value;
}
int Integer(const wchar_t* section, const wchar_t* key, int fallback, int minimum, int maximum) {
    const auto value = Setting(section, key, L"", 64);
    wchar_t* end = nullptr;
    errno = 0;
    const long number = wcstol(value.c_str(), &end, 10);
    if (end == value.c_str() || errno == ERANGE) return fallback;
    while (iswspace(*end)) ++end;
    if (*end) return fallback;
    return static_cast<int>(std::clamp(number, static_cast<long>(minimum), static_cast<long>(maximum)));
}
void RefreshConfiguration() {
    Config config;
    const auto mode = Setting(L"Randomizer", L"Mode", L"random");
    if (!_wcsicmp(mode.c_str(), L"alternate")) config.mode = Mode::Alternate;
    else if (!_wcsicmp(mode.c_str(), L"new")) config.mode = Mode::New;
    else if (!_wcsicmp(mode.c_str(), L"old")) config.mode = Mode::Old;
    config.enabled = Integer(L"Randomizer", L"Enabled", 1, 0, 1) != 0;
    config.logCache = Integer(L"Randomizer", L"LogCache", 0, 0, 1) != 0;
    config.oldPercent = Integer(L"Randomizer", L"OldPercent", 50, 0, 100);
    config.volumePercent = Integer(L"Randomizer", L"VolumePercent", 100, 0, 100);
    config.targetOld = !_wcsicmp(Setting(L"Randomizer", L"GuaranteeTarget", L"new").c_str(), L"old");
    config.guarantee = Integer(L"Randomizer", L"GuaranteeEnabled", 1, 0, 1) != 0;
    config.radiance = Integer(L"Randomizer", L"CapturingRadianceEnabled", 1, 0, 1) != 0;
    config.softPity = Integer(L"Randomizer", L"SoftPityEnabled", 0, 0, 1) != 0;
    config.softPityStart = Integer(L"Randomizer", L"SoftPityStart", 10, 1, 1000);
    config.softPityStep = Integer(L"Randomizer", L"SoftPityStep", 5, 0, 100);
    // Experimental stereo is excluded from this release, including old INIs.
    // Keep its implementation dormant; no stereo configuration is consumed.
    config.stereo = false;
    config.overlay.enabled = Integer(L"Overlay", L"Enabled", 1, 0, 1) != 0;
    config.overlay.left = static_cast<float>(Integer(L"Overlay", L"LeftPercent", 2, 0, 90)) / 100.f;
    config.overlay.center = static_cast<float>(Integer(L"Overlay", L"CenterYPercent", 40, 0, 100)) / 100.f;
    config.overlay.height = static_cast<float>(Integer(L"Overlay", L"HeightPercent", 50, 10, 100)) / 100.f;
    config.overlay.scrollMs = static_cast<float>(Integer(L"Overlay", L"ScrollMs", 180, 0, 2000));
    config.overlay.maxEntries = Integer(L"Overlay", L"MaxEntries", 10, 1, static_cast<int>(kMaxHistory));
    config.overlay.transparency = static_cast<float>(Integer(L"Overlay", L"TransparencyPercent", 0, 0, 100)) / 100.f;
    config.overlay.waiScale = static_cast<float>(Integer(L"Overlay", L"WaiScalePercent", 100, 0, 200)) / 100.f;
    config.overlay.hideOnOtherVoice = Integer(L"Overlay", L"HideOnOtherVoice", 0, 0, 1) != 0;
    {
        std::lock_guard<std::mutex> lock(state->configMutex);
        state->rules.Configure(config);
        state->config = config;
    }
    ConfigureOverlay(config.overlay);
}
uint16_t U16(const std::vector<uint8_t>& data, size_t offset) {
    return static_cast<uint16_t>(data[offset] | (data[offset + 1] << 8));
}
uint32_t U32(const std::vector<uint8_t>& data, size_t offset) {
    uint32_t value;
    memcpy(&value, data.data() + offset, sizeof(value));
    return value;
}
bool ValidWave(const std::wstring& path) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    struct CloseFile { HANDLE file; ~CloseFile() { CloseHandle(file); } } close{file};
    LARGE_INTEGER size{};
    const bool sizeOk = GetFileSizeEx(file, &size) && size.QuadPart >= 44 && size.QuadPart <= 16777216;
    std::vector<uint8_t> data;
    if (sizeOk) data.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    const bool readOk = sizeOk && ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr)
                        && read == data.size();
    if (!readOk || memcmp(data.data(), "RIFF", 4) || memcmp(data.data() + 8, "WAVE", 4)
        || static_cast<uint64_t>(U32(data, 4)) + 8 != data.size()) return false;
    bool format = false, audio = false;
    size_t offset = 12;
    while (offset + 8 <= data.size()) {
        const size_t length = U32(data, offset + 4);
        const size_t payload = offset + 8;
        if (length > data.size() - payload) return false;
        if (!memcmp(data.data() + offset, "fmt ", 4))
            format = length >= 16 && U16(data, payload) == 1 && U16(data, payload + 2) == 1
                && U32(data, payload + 4) == 48000 && U16(data, payload + 12) == 2
                && U16(data, payload + 14) == 16;
        if (!memcmp(data.data() + offset, "data", 4)) audio = length > 0 && !(length & 1);
        offset = payload + length + (length & 1);
    }
    return format && audio;
}
bool AnsiPath(const std::wstring& path) {
    BOOL substituted = FALSE;
    char buffer[MAX_PATH]{};
    const int count = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, path.c_str(), -1,
                                         buffer, MAX_PATH, nullptr, &substituted);
    return count > 0 && count <= MAX_PATH && !substituted;
}
struct Prepared {
    Request request{};
    std::string name;
    uint64_t ticket = 0;
    uint32_t id = 0;
    Decision decision;
    const Resource* resource = nullptr;
    bool eligible = false;
    bool stereoRequested = false, stereo = false, swap = false;
};
bool Prepare(const Request* input, Prepared& play) {
    char name[MAX_PATH]{};
    if (!SafeCopy(&play.request, input, sizeof(Request)) || play.request.kind != 2
        || !SafeName(name, play.request.name)) return false;
    play.name = name;
    const auto ui = state->uiIndex.find(play.name);
    if (ui != state->uiIndex.end()) { play.ticket = ++state->tickets; play.id = ui->second; }
    const auto resource = state->resources.find(play.name);
    std::lock_guard<std::mutex> lock(state->configMutex);
    if (state->config.enabled && resource != state->resources.end()) {
        play.resource = &resource->second;
        play.stereoRequested = state->config.stereo;
        play.swap = state->config.stereoSwap;
        if (play.stereoRequested) {
            play.stereo = true;
            play.decision.reason = 7;
        } else play.decision = state->rules.Choose(play.name, state->config);
        play.eligible = true;
    }
    return true;
}
uint32_t __fastcall PlayHook(void* self, const Request* input) {
    Prepared play;
    bool prepared = false;
    try { prepared = Prepare(input, play); } catch (...) { Log("PLAY preparation failed"); }
    if (!prepared) return originalPlay(self, input);
    bool actualOld = false;
    uint32_t handle;
    if (!play.eligible) handle = originalPlay(self, input);
    else {
        if (!play.stereo && play.decision.selectedOld) {
            if (FileExists(play.resource->path)) {
                play.request.name = play.resource->alias.c_str();
                actualOld = true;
            } else Log("FALLBACK event=%llu name=%s old_file_missing", play.decision.event, play.name.c_str());
        }
        handle = originalPlay(self, &play.request);
        if (actualOld && handle == UINT32_MAX) {
            Log("FALLBACK event=%llu name=%s old_queue_rejected", play.decision.event, play.name.c_str());
            handle = originalPlay(self, input);
            actualOld = false;
            play.stereo = false;
        }
        const auto& d = play.decision;
        Log("PLAY event=%llu id=%u name=%s selectedOld=%d actualOld=%d reason=%u roll=%d count=%u->%u pending=%d->%d targetOld=%d guarantee=%d radiance=%d epoch=%llu handle=%u soft=%u->%u targetPercent=%d stereo=%d swap=%d",
            d.event, play.id, play.name.c_str(), d.selectedOld, actualOld, d.reason, d.roll,
            d.countBefore, d.countAfter, d.pendingBefore, d.pendingAfter, d.targetOld, d.guarantee,
            d.radiance, d.epoch, handle, d.softBefore, d.softAfter, d.targetPercent, play.stereo, play.swap);
    }
    if (handle != UINT32_MAX) NotifyVoicePlayed(play.ticket != 0);
    try { AppendHistory(play.ticket, play.id, play.name, actualOld, play.decision.reason, handle, play.decision.targetOld, play.stereo, play.swap); }
    catch (...) { Log("HISTORY allocation failed; voice selection unaffected"); }
    return handle;
}
int32_t __fastcall CacheHook(void* self, const char* path) {
    const int32_t slot = originalCache(self, path);
    try {
        { std::lock_guard<std::mutex> lock(state->configMutex); if (!state->config.logCache) return slot; }
        char buffer[MAX_PATH]{};
        if (!SafeName(buffer, path)) return slot;
        std::string name(buffer);
        for (char& character : name) {
            if (character == '\\') character = '/';
            else if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
        }
        if (name.compare(0, 10, "voice/wav/") || name.size() < 14 || name.compare(name.size() - 4, 4, ".wav")) return slot;
        name = name.substr(10, name.size() - 14);
        const char* set = "game";
        constexpr char oldPrefix[] = "voice_randomizer/assets/old/";
        constexpr char testPrefix[] = "voice_randomizer/assets/placeholder/";
        if (!name.compare(0, sizeof(oldPrefix)-1, oldPrefix)) { name.erase(0, sizeof(oldPrefix)-1); set = "legacy"; }
        else if (!name.compare(0, sizeof(testPrefix)-1, testPrefix)) { name.erase(0, sizeof(testPrefix)-1); set = "placeholder"; }
        if (state->resources.find(name) != state->resources.end())
            Log("CACHE name=%s set=%s slot=%d path=%s", name.c_str(), set, slot, buffer);
    } catch (...) { Log("CACHE observation failed"); }
    return slot;
}
bool ModuleCacheKey(const char* input, char (&key)[64]) {
    char buffer[MAX_PATH]{};
    if (!SafeName(buffer, input)) return false;
    for (char& character : buffer) {
        if (character == '\\') character = '/';
        else if (character >= 'A' && character <= 'Z') character += 'a'-'A';
    }
    std::string_view name(buffer);
    constexpr std::string_view fullPrefix = "voice/wav/";
    if (name.substr(0, fullPrefix.size()) == fullPrefix) {
        if (name.size() < fullPrefix.size()+4 || name.substr(name.size()-4) != ".wav") return false;
        name.remove_prefix(fullPrefix.size());
        name.remove_suffix(4);
    }
    constexpr std::string_view oldPrefix = "voice_randomizer/assets/old/";
    constexpr std::string_view testPrefix = "voice_randomizer/assets/placeholder/";
    std::string_view keyPrefix;
    if (name.substr(0,oldPrefix.size()) == oldPrefix) {
        name.remove_prefix(oldPrefix.size()); keyPrefix = "vr_old_";
    } else if (name.substr(0,testPrefix.size()) == testPrefix) {
        name.remove_prefix(testPrefix.size()); keyPrefix = "vr_test_";
    } else return false;
    // Exact startup-valid table membership rejects other namespaces, nested
    // paths, extension variants and the three UI-only rows.
    if (state->resources.find(std::string(name)) == state->resources.end()
        || keyPrefix.size()+name.size() >= sizeof(key)) return false;
    memcpy(key,keyPrefix.data(),keyPrefix.size());
    memcpy(key+keyPrefix.size(),name.data(),name.size());
    key[keyPrefix.size()+name.size()] = '\0';
    return true;
}
void __fastcall CacheKeyHook(char* output, const char* input) {
    char key[64]{};
    bool matched = false;
    if (output) {
        try { matched = ModuleCacheKey(input,key); }
        catch (...) {} // Preserve the original helper's behavior on observation failure.
    }
    // The game helper synchronously copies into output64; no retained pointer.
    // All reviewed callers consume output, never RAX (559B20 is void).
    originalCacheKey(output,matched ? key : input);
}
struct CacheLock {
    void* mutex;
    bool held;
    explicit CacheLock(void* manager) : mutex(static_cast<uint8_t*>(manager)+0xC28),
        held(lockAudioCache(mutex)==0) {}
    ~CacheLock() { if (held) unlockAudioCache(mutex); }
};
bool PrecacheOldWave(void* manager,const Resource& resource) {
    // Use the engine's own cache and its native mutex on the audio task thread.
    // Its insert routine copies the PCM payload into engine-owned storage.
    if (!FileExists(resource.path)) return false;
    auto* cache=static_cast<uint8_t*>(manager)+0x2A0;
    const std::string path="voice/wav/"+resource.alias+".wav";
    {
        CacheLock lock(manager);
        if (!lock.held) return false;
        if (originalCache(cache,path.c_str())!=-1) return true;
    }
    const HANDLE file=CreateFileW(resource.path.c_str(),GENERIC_READ,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file==INVALID_HANDLE_VALUE) return false;
    struct Close { HANDLE file; ~Close() { CloseHandle(file); } } close{file};
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file,&size) || size.QuadPart<44 || size.QuadPart>16777216) return false;
    std::vector<uint8_t> wav(static_cast<size_t>(size.QuadPart));
    DWORD count=0;
    if (!ReadFile(file,wav.data(),static_cast<DWORD>(wav.size()),&count,nullptr) || count!=wav.size()) return false;
    void* sampler=nullptr;
    if (!SafeCopy(&sampler,static_cast<uint8_t*>(manager)+0x400,sizeof(sampler)) || !sampler) return false;
    CacheLock lock(manager);
    if (!lock.held) return false;
    return insertWave(cache,path.c_str(),wav.data(),static_cast<uint64_t>(wav.size()),sampler)!=UINT32_MAX;
}
bool __fastcall SamplerHook(void* self,uint32_t slot,const char* name,uint32_t a4,
    uint32_t a5,uint32_t a6,const void* a7,uint32_t flags,
    const WAVEFORMATEX* format,const XAUDIO2_BUFFER* buffer) {
    StereoContext* context=currentStereo;
    WORD channels=0;
    if (format) SafeCopy(&channels,reinterpret_cast<const uint8_t*>(format)+2,sizeof(channels));
    const bool split=context && channels==1;
    if (split) context->attempted=true;
    // A private two-channel submix isolates our inner mono->stereo pan from the
    // engine's subsequent per-frame outer routing and volume updates.
    const bool success=originalSampler(self,slot,name,a4,a5,a6,a7,
        split ? ((flags&0x00ffffffu)|0x01000000u) : flags,format,buffer);
    // Normal-mode channel gain leaves the engine's volume/fade/routing intact.
    // Only an identified Klose task installs this TLS scope; stereo bypasses it.
    if (success && !split && defaultVoiceGain!=1.f && channels>0 && channels<=XAUDIO2_MAX_AUDIO_CHANNELS) {
        IXAudio2SourceVoice** sources=nullptr;
        IXAudio2SourceVoice* source=nullptr;
        uint32_t count=0;
        const auto bytes=static_cast<const uint8_t*>(self);
        if (SafeCopy(&sources,bytes+0xE8,sizeof(sources)) && sources
            && SafeCopy(&count,bytes+0xF0,sizeof(count)) && slot<count
            && SafeCopy(&source,sources+slot,sizeof(source)) && source) {
            std::array<float,XAUDIO2_MAX_AUDIO_CHANNELS> gains;
            std::fill_n(gains.begin(),channels,defaultVoiceGain);
            const HRESULT result=source->SetChannelVolumes(channels,gains.data(),XAUDIO2_COMMIT_NOW);
            Log("VOICE_VOLUME handle=%u slot=%u gain=%g result=%08lx",a4,slot,
                static_cast<double>(defaultVoiceGain),static_cast<unsigned long>(result));
        }
    }
    if (!success || !split) return success;
    IXAudio2SourceVoice** sources=nullptr;
    IXAudio2SubmixVoice** sends=nullptr;
    IXAudio2SourceVoice* source=nullptr;
    IXAudio2SubmixVoice* destination=nullptr;
    uint32_t sourceCount=0,sendCount=0;
    const auto bytes=static_cast<const uint8_t*>(self);
    if (SafeCopy(&sources,bytes+0xE8,sizeof(sources)) && sources
        && SafeCopy(&sends,bytes+0xF8,sizeof(sends)) && sends
        && SafeCopy(&sourceCount,bytes+0xF0,sizeof(sourceCount)) && slot<sourceCount
        && SafeCopy(&sendCount,bytes+0x100,sizeof(sendCount)) && slot<sendCount
        && SafeCopy(&source,sources+slot,sizeof(source)) && source
        && SafeCopy(&destination,sends+slot,sizeof(destination)) && destination) {
        const float pan[2]={context->right?0.f:1.f,context->right?1.f:0.f};
        const HRESULT routed=source->SetOutputMatrix(destination,1,2,pan,XAUDIO2_COMMIT_NOW);
        const HRESULT gained=source->SetChannelVolumes(1,&context->gain,XAUDIO2_COMMIT_NOW);
        context->created=SUCCEEDED(routed) && SUCCEEDED(gained);
        Log("NATIVE_STEREO_ROUTE handle=%u slot=%u side=%s gain=%g matrix=%08lx volume=%08lx",
            a4,slot,context->right?"right":"left",static_cast<double>(context->gain),
            static_cast<unsigned long>(routed),static_cast<unsigned long>(gained));
        if (context->created) {
            context->sampler=self;
            context->slot=slot;
            context->source=source;
            context->destination=destination;
            return true;
        }
    }
    // Do not leave an allocated, unstarted voice behind after a failed route.
    using ClearSlot=void (__fastcall*)(void*,uint32_t);
    const auto vtable=*reinterpret_cast<void***>(self);
    reinterpret_cast<ClearSlot>(vtable[0x120/sizeof(void*)])(self,slot);
    return false;
}
bool Started(const StereoContext& context) {
    if (!context.created || !context.sampler) return false;
    const auto bytes=static_cast<const uint8_t*>(context.sampler);
    const uint8_t* metadata=nullptr;
    uint32_t count=0;
    uint8_t started=0;
    IXAudio2SourceVoice** sources=nullptr;
    IXAudio2SourceVoice* source=nullptr;
    return SafeCopy(&sources,bytes+0xE8,sizeof(sources)) && sources
        && SafeCopy(&count,bytes+0xF0,sizeof(count)) && context.slot<count
        && SafeCopy(&source,sources+context.slot,sizeof(source)) && source==context.source
        && SafeCopy(&metadata,bytes+0xA8,sizeof(metadata)) && metadata
        && SafeCopy(&count,bytes+0xB0,sizeof(count)) && context.slot<count
        && SafeCopy(&started,metadata+context.slot*0x180+1,sizeof(started)) && started!=0;
}
void RestorePrimary(const StereoContext& context) {
    if (!Started(context)) return;
    IXAudio2SourceVoice** sources=nullptr;
    IXAudio2SourceVoice* source=nullptr;
    if (!SafeCopy(&sources,static_cast<const uint8_t*>(context.sampler)+0xE8,sizeof(sources)) || !sources
        || !SafeCopy(&source,sources+context.slot,sizeof(source)) || source!=context.source) return;
    const float pan[2]={1.f,1.f}, gain=1.f;
    source->SetOutputMatrix(context.destination,1,2,pan,XAUDIO2_COMMIT_NOW);
    source->SetChannelVolumes(1,&gain,XAUDIO2_COMMIT_NOW);
}
uint32_t __fastcall ProcessTaskHook(void* manager,const void* task) {
    bool primaryCalled=false;
    uint32_t primaryResult=1;
    StereoContext primary,secondary;
    try {
        const Request* input=nullptr;
        Request request{};
        char name[MAX_PATH]{};
        if (!SafeCopy(&input,static_cast<const uint8_t*>(task)+0x10,sizeof(input)) || !input
            || !SafeCopy(&request,input,sizeof(request)) || request.kind!=2 || !SafeName(name,request.name))
            return originalProcessTask(manager,task);
        const auto resource=state->resources.find(name);
        Config config;
        { std::lock_guard<std::mutex> lock(state->configMutex); config=state->config; }
        if (!config.enabled)
            return originalProcessTask(manager,task);
        if (!config.stereo) {
            char key[64]{};
            if (config.volumePercent==100 || (state->uiIndex.find(name)==state->uiIndex.end() && !ModuleCacheKey(name,key)))
                return originalProcessTask(manager,task);
            DefaultVolumeScope volume(static_cast<float>(config.volumePercent)/100.f);
            return originalProcessTask(manager,task);
        }
        if (resource==state->resources.end())
            return originalProcessTask(manager,task);
        if (!PrecacheOldWave(manager,resource->second)) {
            Log("NATIVE_STEREO_FALLBACK name=%s old_cache_unavailable",name);
            return originalProcessTask(manager,task);
        }
        // This handler only reads task+0, +10, +68 and +70. Reuse the public
        // handle in a private header, preserving all its timing metadata.
        alignas(16) std::array<uint8_t,0x78> secondaryTask{};
        if (!SafeCopy(secondaryTask.data(),task,secondaryTask.size()))
            return originalProcessTask(manager,task);
        Request oldRequest=request;
        oldRequest.name=resource->second.alias.c_str();
        const uint32_t anonymous=UINT32_MAX;
        // A secondary source must not compete with the primary's explicit
        // identity. Its distinct vr_old_/vr_test_ cache key remains intact.
        memcpy(reinterpret_cast<uint8_t*>(&oldRequest)+0x38,&anonymous,sizeof(anonymous));
        const Request* oldPointer=&oldRequest;
        memcpy(secondaryTask.data()+0x10,&oldPointer,sizeof(oldPointer));
        primary={config.stereoSwap,false,
            static_cast<float>(config.stereoSwap?config.stereoRight:config.stereoLeft)/100.f};
        {
            StereoScope scope(primary);
            primaryCalled=true;
            primaryResult=originalProcessTask(manager,task);
        }
        if (!Started(primary)) {
            Log("NATIVE_STEREO_FALLBACK name=%s primary_not_started result=%u",name,primaryResult);
            // A cold-cache pending result has not touched the sampler. Only a
            // failed native creation/routing/start is retried in ordinary mono.
            if (primary.attempted) primaryResult=originalProcessTask(manager,task);
            return primaryResult;
        }
        secondary={!config.stereoSwap,false,
            static_cast<float>(config.stereoSwap?config.stereoLeft:config.stereoRight)/100.f};
        uint32_t secondaryResult=1;
        {
            StereoScope scope(secondary);
            secondaryResult=originalProcessTask(manager,secondaryTask.data());
        }
        const bool both=primary.slot!=secondary.slot && Started(primary) && Started(secondary);
        if (!both) {
            if (Started(secondary)) {
                using ClearSlot=void (__fastcall*)(void*,uint32_t);
                const auto vtable=*reinterpret_cast<void***>(secondary.sampler);
                reinterpret_cast<ClearSlot>(vtable[0x120/sizeof(void*)])(secondary.sampler,secondary.slot);
            }
            if (Started(primary)) RestorePrimary(primary);
            else primaryResult=originalProcessTask(manager,task);
        }
        uint32_t handle=0;
        memcpy(&handle,secondaryTask.data(),sizeof(handle));
        Log("NATIVE_STEREO_PAIR handle=%u name=%s both=%d secondary_result=%u swap=%d",
            handle,name,both,secondaryResult,config.stereoSwap);
        return primaryResult;
    } catch (...) {
        RestorePrimary(primary);
        Log("NATIVE_STEREO_FALLBACK native preparation failed");
        return primaryCalled?primaryResult:originalProcessTask(manager,task);
    }
}
uint32_t __fastcall FrameHook(void* self, uint8_t flag) {
    IDXGISwapChain* chain = nullptr;
    if (SafeCopy(&chain, static_cast<const uint8_t*>(self) + 0x2D8, sizeof(chain))) DrawHistory(chain);
    return originalFrame(self, flag);
}
DWORD WINAPI ConfigurationWorker(void*) {
    for (;;) {
        Sleep(1000);
        try { RefreshConfiguration(); } catch (...) { Log("CONFIG refresh failed; retaining prior configuration"); }
    }
}
DWORD AttachHooks() {
    // Gather/allocate before suspension. No module allocations after UpdateThread.
    struct Threads {
        std::vector<HANDLE> values;
        ~Threads() { for (HANDLE handle : values) CloseHandle(handle); }
    } threads;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return GetLastError();
    {
        struct CloseSnapshot { HANDLE value; ~CloseSnapshot() { CloseHandle(value); } } close{snapshot};
        THREADENTRY32 entry{}; entry.dwSize = sizeof(entry);
        if (!Thread32First(snapshot, &entry)) return GetLastError();
        do {
            if (entry.th32OwnerProcessID != GetCurrentProcessId() || entry.th32ThreadID == GetCurrentThreadId()) continue;
            const HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID);
            if (!thread) return GetLastError();
            try { threads.values.push_back(thread); }
            catch (...) { CloseHandle(thread); throw; }
        } while (Thread32Next(snapshot, &entry));
        if (GetLastError() != ERROR_NO_MORE_FILES) return GetLastError();
    }
    LONG error = DetourTransactionBegin();
    if (error != NO_ERROR) return static_cast<DWORD>(error);
    error = DetourUpdateThread(GetCurrentThread());
    // Enlist existing game threads so Detours can relocate any instruction pointers.
    for (HANDLE thread : threads.values) {
        if (error) break;
        error = DetourUpdateThread(thread);
    }
    if (!error) error = DetourAttach(reinterpret_cast<PVOID*>(&originalPlay), PlayHook);
    if (!error) error = DetourAttach(reinterpret_cast<PVOID*>(&originalCache), CacheHook);
    if (!error) error = DetourAttach(reinterpret_cast<PVOID*>(&originalFrame), FrameHook);
    if (!error) error = DetourAttach(reinterpret_cast<PVOID*>(&originalCacheKey), CacheKeyHook);
    if (!error) error = DetourAttach(reinterpret_cast<PVOID*>(&originalSampler), SamplerHook);
    if (!error) error = DetourAttach(reinterpret_cast<PVOID*>(&originalProcessTask), ProcessTaskHook);
    if (error) DetourTransactionAbort();
    else error = DetourTransactionCommit();
    return static_cast<DWORD>(error);
}
DWORD Initialize() {
    wchar_t executable[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
    if (!length || length >= 32768) return ERROR_BAD_PATHNAME;
    const std::wstring exe(executable, length);
    auto* addressPlay = targets::Play();
    auto* addressCache = targets::Cache();
    auto* addressFrame = targets::Frame();
    auto* addressCacheKey = targets::CacheKey();
    auto* addressSampler = targets::Sampler();
    auto* addressProcessTask = targets::ProcessTask();
    auto* addressInsertWave = targets::InsertWave();
    auto* addressLockCache = targets::LockCache();
    auto* addressUnlockCache = targets::UnlockCache();
    if (!addressPlay || !addressCache || !addressFrame || !addressCacheKey || !addressSampler || !addressProcessTask || !addressInsertWave || !addressLockCache || !addressUnlockCache) return ERROR_PROC_NOT_FOUND;
    uint64_t seed = GetCurrentProcessId() ^ GetTickCount64();
    (void)BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&seed), sizeof(seed), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    std::unique_ptr<State> created(new State(seed));
    created->root = exe.substr(0, exe.find_last_of(L"\\/")) + L"\\voice\\wav\\voice_randomizer";
    created->ini = created->root + L"\\voice_randomizer.ini";
    if (!FileExists(created->ini)) return ERROR_FILE_NOT_FOUND;
    state = created.release();
    SetLogPath(state->root);
    InitializeOverlay(state->root);
    const bool placeholder = !_wcsicmp(Setting(L"Randomizer", L"ReplacementSet", L"legacy", 32).c_str(), L"placeholder");
    const std::string relative = placeholder ? "voice_randomizer/assets/placeholder/" : "voice_randomizer/assets/old/";
    const std::wstring folder = state->root + (placeholder ? L"\\assets\\placeholder\\" : L"\\assets\\old\\");
    state->resources.reserve(824);
    for (size_t index = 0; index < 827; ++index) {
        const auto& item = kIndex[index];
        state->uiIndex.emplace(item.name, item.id);
        if (index >= 824) continue;
        const std::string name(item.name);
        const std::wstring path = folder + std::wstring(name.begin(), name.end()) + L".wav";
        if (AnsiPath(path) && ValidWave(path)) {
            Resource resource{item.id,relative+name,path};
            state->resources.emplace(name,std::move(resource));
        }
        else Log("RESOURCE excluded id=%u name=%s", item.id, item.name);
    }
    RefreshConfiguration();
    originalPlay = reinterpret_cast<Play>(addressPlay);
    originalCache = reinterpret_cast<Cache>(addressCache);
    originalFrame = reinterpret_cast<Frame>(addressFrame);
    originalCacheKey = reinterpret_cast<CacheKey>(addressCacheKey);
    originalSampler = reinterpret_cast<Sampler>(addressSampler);
    originalProcessTask=reinterpret_cast<ProcessTask>(addressProcessTask);
    insertWave=reinterpret_cast<InsertWave>(addressInsertWave);
    lockAudioCache=reinterpret_cast<NativeMutex>(addressLockCache);
    unlockAudioCache=reinterpret_cast<NativeMutex>(addressUnlockCache);
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&Initialize), &pinned)) return GetLastError();
    const DWORD status = AttachHooks();
    if (status) { Log("INIT hook transaction failed error=%lu", status); return status; }
    const HANDLE worker = CreateThread(nullptr, 0, ConfigurationWorker, nullptr, 0, nullptr);
    if (worker) CloseHandle(worker);
    else Log("CONFIG worker failed error=%lu; initial configuration retained", GetLastError());
    Log("INIT ready resources=%zu ui=827 set=%s alias_namespace=voice_randomizer/assets hooks=6 target_lookup=signature no_open_hook=1 runtime_validation=pending",
        state->resources.size(), placeholder ? "placeholder" : "legacy");
    return ERROR_SUCCESS;
}
}
void SetLogPath(const std::wstring& root) { logPath = root + L"\\VoiceRandomizer.log"; }
void Log(const char* format, ...) noexcept {
    try {
        std::lock_guard<std::mutex> lock(logMutex);
        if (logPath.empty()) return;
        char buffer[2048]{};
        va_list arguments;
        va_start(arguments, format);
        const int count = vsnprintf(buffer, sizeof(buffer), format, arguments);
        va_end(arguments);
        if (count < 0) return;
        const HANDLE file = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        DWORD written;
        WriteFile(file, buffer, static_cast<DWORD>(std::min<size_t>(static_cast<size_t>(count), sizeof(buffer)-1)), &written, nullptr);
        WriteFile(file, "\r\n", 2, &written, nullptr);
        CloseHandle(file);
    } catch (...) {}
}
}
extern "C" __declspec(dllexport) DWORD WINAPI SoraVoiceInitialize(void) {
    if (InterlockedCompareExchange(&voice::initialization, 1, 0) != 0) {
        while (InterlockedCompareExchange(&voice::initialization, 0, 0) == 1) Sleep(1);
        return voice::initializationStatus;
    }
    try { voice::initializationStatus = voice::Initialize(); }
    catch (...) { voice::initializationStatus = ERROR_UNHANDLED_EXCEPTION; }
    InterlockedExchange(&voice::initialization, 2);
    return voice::initializationStatus;
}
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
