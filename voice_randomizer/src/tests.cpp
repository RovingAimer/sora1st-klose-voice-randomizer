#include "plugin.cpp"
#include <thread>
#include <stdexcept>
#include <iostream>
#include <algorithm>

namespace {
void check(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
std::mutex fake_mutex;
std::vector<std::string> requested;
std::unordered_map<std::string,unsigned> fake_cache;
std::string last_open;
__declspec(noinline) uint32_t __fastcall fake_play(void*,const vr::PlayRequest* req) {
    std::lock_guard<std::mutex> guard(fake_mutex);
    requested.emplace_back(req->name);
    fake_cache.emplace(req->name,static_cast<unsigned>(fake_cache.size()));
    return 73;
}
int64_t __fastcall fake_open(int64_t,const char* path,uint32_t,uint32_t,uint16_t) { last_open=path; return 123; }
uint32_t __fastcall reject_play(void*,const vr::PlayRequest*) { return UINT32_MAX; }
unsigned char frame_sync=0;
uint64_t __fastcall fake_frame(void*,unsigned char sync) { frame_sync=sync; return 0x12345678; }
uint32_t __fastcall fake_find(void*,const char* path) { return strstr(path,"vr_old")?12:11; }
#include "config_tests.h"
void test_random_rules() {
    using R=vr::Reason;
    for (unsigned radiance_roll : {0u,99u}) {
        const std::vector<unsigned> rolls{0,0,0,radiance_roll,0};
        size_t draws=0;
        vr::Selector s(1,[&] { check(draws<rolls.size(),"unexpected RNG draw"); return rolls[draws++]; });
        const R reasons[]{R::RandomOld,R::Guarantee,R::RandomOld,R::Guarantee,R::RandomOld,R::Guarantee,R::CapturingRadiance};
        const unsigned counts[]{1,1,2,2,3,3,0};
        for (unsigned i=0;i<7;++i) {
            auto x=s.choose(i%2?"v006_b0078":"v006_b0021",vr::Mode::Random,50);
            check(x.reason==reasons[i] && x.count_after==counts[i],"old/guarantee/old/guarantee/old/guarantee/radiance trace");
            check(x.old==(i==0 || i==2 || i==4),"only normal random old emits old");
            if (i==5) check(x.roll==-1 && x.count_before==3 && x.count_after==3,"guarantee takes priority at count three without RNG");
            if (i==6) check(x.roll==static_cast<int>(radiance_roll) && !x.guarantee_before && !x.guarantee_after,"radiance overrides either raw outcome without using or scheduling guarantee");
        }
        check(draws==4,"three old rolls plus radiance roll; guarantees consume no draws");
        auto next=s.choose("v006_b0001",vr::Mode::Random,50);
        check(next.reason==R::RandomOld && next.count_before==0 && next.count_after==1 && next.guarantee_after,"radiance leaves next random opportunity intact");
    }
    {
        const unsigned rolls[]{0,0,99,99,99}; size_t draws=0;
        vr::Selector s(1,[&] { check(draws<5,"unexpected decrement test draw"); return rolls[draws++]; });
        const unsigned counts[]{1,1,2,2,1,0,0};
        for (unsigned i=0;i<7;++i) {
            auto x=s.choose("same",vr::Mode::Random,50);
            check(x.count_after==counts[i],"natural new decrements, guarantee does not, zero saturates");
        }
        check(draws==5,"decrement test guarantee skips RNG");
    }
    {
        unsigned draws=0;
        vr::Selector s(1,[&] { ++draws; return 0u; });
        s.choose("first",vr::Mode::Random,50);
        for (auto m:{vr::Mode::New,vr::Mode::Old,vr::Mode::Alternate}) {
            auto x=s.choose("different",m,50);
            check(x.roll==-1 && x.count_after==1 && x.guarantee_after,"diagnostic modes pause both states");
        }
        auto x=s.choose("third",vr::Mode::Random,50);
        check(x.reason==R::Guarantee && !x.old && x.count_after==1 && draws==1,"pending guarantee carries across filenames and debug modes");
    }
    std::cout<<"PASS: guarantee priority, no-draw guarantee, count floor/decrement, both radiance raw outcomes, shared state, debug pause\n";
    vr::Selector distribution(985);
    unsigned outputs_old=0,draws=0,raw_old=0,guarantees=0,radiances=0;
    bool previous_old=false;
    for (unsigned i=0;i<10000;++i) {
        auto x=distribution.choose("v006_b0001",vr::Mode::Random,50);
        check(!(previous_old && x.old),"random mode cannot emit consecutive old choices");
        if (previous_old) check(x.reason==R::Guarantee,"every random old is followed by guarantee");
        if (x.roll>=0) { ++draws; raw_old+=x.roll<50; }
        outputs_old+=x.old; guarantees+=x.reason==R::Guarantee; radiances+=x.reason==R::CapturingRadiance;
        check(x.count_after<=3,"counter bounded by radiance threshold");
        previous_old=x.old;
    }
    check(raw_old>draws*47/100 && raw_old<draws*53/100,"base RNG still 50 percent, excluding guarantee skips");
    check(outputs_old>=guarantees && outputs_old-guarantees<=1 && radiances>0,"guarantee coverage and radiance observed");
    std::cout<<"PASS: 10000 choices; base draws="<<draws<<" raw old="<<raw_old<<" final old="<<outputs_old<<" guarantee="<<guarantees<<" radiance="<<radiances<<'\n';
    vr::Selector concurrent(761);
    std::mutex results_mutex;
    std::vector<vr::Selection> results;
    std::vector<std::thread> threads;
    for (unsigned i=0;i<8;++i) threads.emplace_back([&,i] {
        for (unsigned j=0;j<250;++j) {
            auto x=concurrent.choose(std::to_string(i),vr::Mode::Random,50);
            std::lock_guard<std::mutex> lock(results_mutex); results.push_back(x);
        }
    });
    for (auto& t:threads) t.join();
    std::sort(results.begin(),results.end(),[](auto& a,auto& b) { return a.sequence<b.sequence; });
    check(results.size()==2000,"concurrent choices not lost");
    for (size_t i=0;i<results.size();++i) {
        check(results[i].sequence==i+1,"concurrent event sequence unique and ordered");
        if (i) {
            check(results[i].count_before==results[i-1].count_after && results[i].guarantee_before==results[i-1].guarantee_after,"atomic rule state across threads");
            if (results[i-1].old) check(results[i].reason==R::Guarantee,"guarantee survives cross-thread names");
        }
    }
    std::cout<<"PASS: 2000 concurrent random decisions maintain a single consistent rule state\n";
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        test_random_rules();
        test_configurable_rules();
        if (argc==2 && std::wstring(argv[1])==L"--rules-only") {
            std::cout<<"PASS: source-only rule tests (game/audio integration tests skipped)\n";
            return 0;
        }
        check(argc==2,"game root argument required");
        std::filesystem::path root(argv[1]);
        test_ini_config(root);
        check(sha256((root/L"sora_1st.exe").wstring())=="9585d3d993874d65156aba633c00b8a25faab3afde8953a8a183c80fb8f657ac","EXE SHA256 guard");
        const auto old=root/L"voice_randomizer/assets/old";
        for (auto& def:kAssets) check(valid_wav((old/(std::string(def.name)+".wav")).wstring()),def.name);
        const auto placeholder=root/L"voice_randomizer/assets/placeholder";
        for (auto& def:kAssets) check(valid_wav((placeholder/(std::string(def.name)+".wav")).wstring()),def.name);
        std::cout<<"PASS: EXE guard and all "<<std::size(kAssets)<<" legacy + placeholder WAV pairs accepted by runtime parser\n";
        auto source=(old/L"v006_b0001.wav").wstring();
        assets.emplace("v006_b0001",Asset{1910,"vr_old_v006_b0001","test-old.wav",source});
        selector=std::make_unique<vr::Selector>(42);
        original_play=fake_play; original_open=fake_open; original_cache=fake_find;
        mode.store(vr::Mode::Alternate);
        vr::PlayRequest req{}; req.category=2; req.name="v006_b0001";
        req.rest.fill(std::byte{0x35});
        vr::PlayRequest before=req;
        for (unsigned i=0;i<20;++i) check(play_hook(nullptr,&req)==73,"play handle preserved");
        check(memcmp(&before,&req,sizeof(req))==0,"caller request unchanged");
        check(fake_cache.size()==2,"only two stable cached versions");
        for (unsigned i=0;i<20;++i) check(requested[i]==(i%2?"vr_old_v006_b0001":"v006_b0001"),"alternate after cache warmed");
        std::cout<<"PASS: actual play hook alternates 20 repeated requests across two stable cache keys\n";
        mode.store(vr::Mode::Random); percent.store(0);
        play_hook(nullptr,&req); check(requested.back()=="v006_b0001","0 percent");
        percent.store(100); play_hook(nullptr,&req); check(requested.back()=="vr_old_v006_b0001","100 percent");
        enabled.store(false); play_hook(nullptr,&req); check(requested.back()=="v006_b0001","disabled passthrough");
        enabled.store(true); req.category=1; play_hook(nullptr,&req); check(requested.back()=="v006_b0001","SE category untouched");
        req.category=2; req.name="v006_b0100"; play_hook(nullptr,&req); check(requested.back()=="v006_b0100","uncovered name untouched");
        req.name="v001_b0001"; play_hook(nullptr,&req); check(requested.back()=="v001_b0001","other character untouched");
        req.name="v006_02_0001"; play_hook(nullptr,&req); check(requested.back()=="v006_02_0001","name absent from temporary test map untouched");
        req.name="v006_b0001";
        play_hook(nullptr,&req); check(requested.back()=="v006_b0001","disabled and excluded requests did not consume pending guarantee");
        mode.store(vr::Mode::Old); // Asset routing tests bypass the random policy.
        assets.at(req.name).wide=L"Z:\\this_file_does_not_exist_vr.wav";
        play_hook(nullptr,&req); check(requested.back()=="v006_b0001","missing old fallback");
        open_hook(0,"voice\\wav\\vr_old_v006_b0001.wav",0,0,0);
        check(last_open=="voice/wav/v006_b0001.wav","asynchronous missing old fallback");
        assets.at(req.name).wide=source;
        open_hook(0,"voice\\wav\\vr_old_v006_b0001.wav",0,0,0); check(last_open=="test-old.wav","old alias redirected");
        open_hook(0,"voice/wav/v006_b0001.wav",0,0,0); check(last_open=="voice/wav/v006_b0001.wav","new pack request unchanged");
        open_hook(0,"voice/wav/vr_old_../v006_b0001.wav",0,0,0); check(last_open=="voice/wav/vr_old_../v006_b0001.wav","path traversal not interpreted");
        check(cache_hook(nullptr,"voice/wav/v006_b0001.wav")==11,"new cache result preserved");
        check(cache_hook(nullptr,"voice/wav/vr_old_v006_b0001.wav")==12,"old cache result preserved");
        std::cout<<"PASS: probabilities, disabled mode, scope exclusions, missing file fallback, path filter\n";
        auto legacy_asset=assets.at(req.name);
        assets.at(req.name)=Asset{1910,"vr_test_v006_b0001","test-placeholder.wav",(placeholder/L"v006_b0001.wav").wstring(),"placeholder"};
        play_hook(nullptr,&req);
        check(requested.back()=="vr_test_v006_b0001","placeholder has independent cache name");
        open_hook(0,"voice\\wav\\vr_test_v006_b0001.wav",0,0,0);
        check(last_open=="test-placeholder.wav","placeholder routes to separate directory");
        open_hook(0,"voice/wav/vr_test_../v006_b0001.wav",0,0,0);
        check(last_open=="voice/wav/vr_test_../v006_b0001.wav","placeholder rejects traversal");
        assets.at(req.name).wide=L"Z:\\this_placeholder_does_not_exist_vr.wav";
        play_hook(nullptr,&req); check(requested.back()=="v006_b0001","missing placeholder falls back to game, not legacy");
        open_hook(0,"voice/wav/vr_test_v006_b0001.wav",0,0,0);
        check(last_open=="voice/wav/v006_b0001.wav","asynchronous placeholder fallback");
        assets.at(req.name)=legacy_asset;
        std::cout<<"PASS: placeholder routing, separate cache identity, missing placeholder fallback\n";
        unsigned count=0;
        mode.store(vr::Mode::Alternate); requested.clear();
        std::vector<std::thread> workers;
        for (unsigned i=0;i<8;++i) workers.emplace_back([&] { for (unsigned j=0;j<100;++j) play_hook(nullptr,&req); });
        for (auto& t:workers) t.join();
        check(requested.size()==800,"concurrent requests retained");
        count=0; for (auto& name:requested) count+=name=="vr_old_v006_b0001";
        check(count==400,"concurrent alternate selection");
        check(fake_cache.size()<10,"cache bounded under repeated calls");
        std::cout<<"PASS: 800 concurrent actual hook calls\n";
        // Exercise an actual native trampoline, not just a direct C++ call.
        check(MH_Initialize()==MH_OK,"MinHook initialize");
        check(MH_CreateHook(reinterpret_cast<void*>(fake_play),reinterpret_cast<void*>(play_hook),reinterpret_cast<void**>(&original_play))==MH_OK,"native trampoline created");
        check(MH_EnableHook(reinterpret_cast<void*>(fake_play))==MH_OK,"native trampoline enabled");
        mode.store(vr::Mode::Old);
        Play volatile indirect=fake_play;
        check(indirect(nullptr,&req)==73,"native detour return");
        check(requested.back()=="vr_old_v006_b0001","native detour selected old");
        check(MH_DisableHook(MH_ALL_HOOKS)==MH_OK,"native detour disabled");
        check(MH_Uninitialize()==MH_OK,"MinHook shutdown");
        std::cout<<"PASS: native x64 MinHook detour and trampoline\n";
        original_play=fake_play;
        for (bool test_set : {false,true}) {
            assets.clear(); requested.clear(); fake_cache.clear();
            selector=std::make_unique<vr::Selector>(42);
            const auto folder=test_set?placeholder:old;
            const std::string prefix=test_set?"vr_test_":"vr_old_";
            for (const auto& def:kAssets) {
                const auto file=folder/(std::string(def.name)+".wav");
                check(vr::voice_name(def.name),"manifest name fits safe basename grammar");
                check(assets.emplace(def.name,Asset{def.id,prefix+def.name,file.string(),file.wstring(),test_set?"placeholder":"legacy"}).second,"unique full-scope manifest name");
            }
            for (const auto& def:kAssets) {
                req.name=def.name;
                mode.store(vr::Mode::Old); play_hook(nullptr,&req);
                const auto& asset=assets.at(def.name);
                check(requested.back()==asset.alias,"every covered request routes to replacement alias");
                const auto virtual_path="voice/wav/"+asset.alias+".wav";
                open_hook(0,virtual_path.c_str(),0,0,0);
                check(last_open==asset.disk,"every covered alias opens corresponding asset");
                mode.store(vr::Mode::New); play_hook(nullptr,&req);
                check(requested.back()==def.name,"every covered request can switch back to game audio");
            }
            check(fake_cache.size()==2*std::size(kAssets),"exactly two distinct cache names per voice in active set");
            mode.store(vr::Mode::Random); percent.store(100);
            req.name="v006_02_0001"; play_hook(nullptr,&req);
            check(requested.back()==prefix+req.name,"story ordinary old schedules global guarantee");
            for (const char* excluded:{"v006_b0100","v006_pv_0001","v006_pv_0002","v001_b0001"}) {
                req.name=excluded; play_hook(nullptr,&req);
                check(requested.back()==excluded,"uncovered voice passes through without consuming guarantee");
                const auto virtual_path="voice/wav/"+prefix+excluded+".wav";
                open_hook(0,virtual_path.c_str(),0,0,0);
                check(last_open==virtual_path,"unknown alias remains outside asset allowlist");
            }
            req.name="v006_b0001"; play_hook(nullptr,&req);
            check(requested.back()==req.name,"story guarantee consumed by next covered battle request");
            std::cout<<"PASS: all "<<std::size(kAssets)<<" names route both branches in "<<(test_set?"placeholder":"legacy")<<" mode; shared story/battle guarantee, exclusions preserved\n";
        }
        for (const char* path:{"voice/wav/vr_test_v006_02_0001/evil.wav", "voice/wav/vr_old_v006_02_../0001.wav", "voice/wav/vr_test_v006_02_0001.wav.extra", "other/wav/vr_test_v006_02_0001.wav"}) {
            std::string name; bool is_old=false;
            check(!vr::parse_voice_path(path,name,is_old),"extended parser rejects unsafe or unrelated paths");
        }
        check(history_voices().size()==827,"history covers all 827 known Klose names");
        req.name="v006_02_0001"; mode.store(vr::Mode::Random); percent.store(50);
        selector=std::make_unique<vr::Selector>(42,[]{return 0u;});
        for(unsigned i=0;i<7;++i) play_hook(nullptr,&req);
        auto history=vr::ui::history().snapshot();
        using B=vr::ui::Badge;
        const B expected[]{B::Wai,B::None,B::Wai,B::None,B::Wai,B::None,B::Radiance};
        for(unsigned i=0;i<7;++i) {
            const auto& entry=history[history.size()-7+i];
            check(entry.badge==expected[i] && entry.old==(i==0 || i==2 || i==4),"actual voice hook emits correct avatars and only two special badges");
        }
        original_play=reject_play;
        const auto last_ticket=history.back().ticket;
        play_hook(nullptr,&req);
        check(vr::ui::history().snapshot().back().ticket==last_ticket,"rejected queue and fallback do not add UI records");
        original_play=fake_play; enabled.store(false); req.name="v006_pv_0001"; play_hook(nullptr,&req);
        auto row=vr::ui::history().snapshot().back();
        check(row.name==req.name && !row.old && row.badge==B::None,"unreplaced Klose voice records game avatar while randomizer disabled");
        req.name="v001_b0001"; play_hook(nullptr,&req);
        check(vr::ui::history().snapshot().back().ticket==row.ticket,"other characters do not enter history");
        enabled.store(true); req.name="v006_b0001"; mode.store(vr::Mode::Old); play_hook(nullptr,&req);
        row=vr::ui::history().snapshot().back(); check(row.old && row.badge==B::None,"forced old has no wai badge");
        cache_hook(nullptr,"voice/wav/v006_b0001.wav");
        check(vr::ui::history().snapshot().back().ticket==row.ticket,"cache retries do not enter history");
        std::array<unsigned char,0x2e0> fake_device{}; original_frame=fake_frame;
        check(frame_hook(fake_device.data(),1)==0x12345678 && frame_sync==1,"frame hook forwards original arguments and return");
        std::cout<<"PASS: live hook history semantics, 827-name coverage, rejection/exclusion filtering, no cache duplication, frame forwarding\nALL TESTS PASSED\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
