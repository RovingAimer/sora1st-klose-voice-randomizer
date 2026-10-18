// Included by tests.cpp, after plugin.cpp and the test helpers.
void test_configurable_rules() {
    using R=vr::Reason;
    for (bool target_old : {false,true}) for (bool guarantee : {false,true}) for (bool radiance : {false,true}) {
        const vr::Rules rules{target_old,guarantee,radiance};
        unsigned draws=0;
        vr::Selector s(1,[&] { ++draws; return target_old?99u:0u; });
        s.configure_rules(rules);
        // Three misses, each followed by an optional guarantee, then a fourth draw.
        for (unsigned miss=1;miss<=3;++miss) {
            auto x=s.choose("story",vr::Mode::Random,50);
            check(x.old!=target_old && x.reason==(target_old?R::RandomNew:R::RandomOld),"random miss uses actual version reason for either target");
            check(x.count_after==(radiance?miss:0) && x.guarantee_after==guarantee,"each switch controls only its own state");
            check(vr::ui::badge_for(x.reason,target_old)==vr::ui::Badge::Wai,"miss badge follows configured target even with mechanisms off");
            if (guarantee) {
                auto g=s.choose("battle",vr::Mode::Random,50);
                check(g.reason==R::Guarantee && g.old==target_old && g.roll==-1,"guarantee favors chosen target and skips RNG");
                check(g.count_after==x.count_after && !g.guarantee_after,"guarantee leaves count unchanged and clears pending state");
            }
        }
        check(draws==3,"exact draw count with independent guarantee switch");
        auto fourth=s.choose("field",vr::Mode::Random,50);
        check(draws==4 && fourth.rules==rules,"selection carries exact rule snapshot");
        check(fourth.reason==(radiance?R::CapturingRadiance:(target_old?R::RandomNew:R::RandomOld)),"radiance independent of guarantee");
        check(fourth.old==(radiance?target_old:!target_old) && fourth.count_after==0,"fourth draw overrides and resets only when radiance enabled");
        check(fourth.guarantee_after==(!radiance && guarantee),"radiance never creates guarantee");
        // Base percent always means OLD, even when the preferred target changes.
        vr::Selector base(1,[]{ return 25u; }); base.configure_rules(rules);
        auto low=base.choose("low",vr::Mode::Random,20);
        check(!low.old,"20 percent with roll 25 always draws new");
        vr::Selector base2(1,[]{ return 25u; }); base2.configure_rules(rules);
        check(base2.choose("high",vr::Mode::Random,30).old,"30 percent with roll 25 always draws old");
    }
    for (bool target_old : {false,true}) for (unsigned final_roll : {0u,99u}) {
        unsigned n=0;
        vr::Selector s(1,[&] { return n++<3?(target_old?99u:0u):final_roll; });
        s.configure_rules({target_old,false,true});
        for(unsigned i=0;i<3;++i) s.choose("same",vr::Mode::Random,50);
        const auto x=s.choose("same",vr::Mode::Random,50);
        check(x.reason==R::CapturingRadiance && x.old==target_old && x.roll==static_cast<int>(final_roll),"radiance overrides either raw draw for either target");
        unsigned draw=0;
        vr::Selector decrement(1,[&] { return draw++<2?(target_old?99u:0u):(target_old?0u:99u); });
        decrement.configure_rules({target_old,false,true});
        for (unsigned expected : {1u,2u,1u,0u,0u})
            check(decrement.choose("same",vr::Mode::Random,50).count_after==expected,"radiance alone increments misses, decrements target hits and floors at zero");
    }
    // Rule changes clear BOTH state components, including while diagnostic modes pause draws.
    for (vr::Rules changed : {vr::Rules{true,true,true},vr::Rules{false,false,true},vr::Rules{false,true,false}}) {
        vr::Selector s(1,[]{return 0u;});
        auto first=s.choose("first",vr::Mode::Random,50);
        s.configure_rules({});
        auto paused=s.choose("paused",vr::Mode::New,50);
        check(paused.guarantee_after && paused.count_after==1 && paused.rules_epoch==first.rules_epoch,"unchanged reload keeps state");
        s.configure_rules(changed);
        auto reset=s.choose("paused",vr::Mode::Old,50);
        check(!reset.guarantee_before && reset.count_before==0 && reset.rules_epoch==first.rules_epoch+1,"changed target or switch clears pending state while paused");
        s.configure_rules({});
        auto next=s.choose("resumed",vr::Mode::Random,50);
        check(next.reason==R::RandomOld && next.count_before==0 && next.rules_epoch==first.rules_epoch+2,"re-enabling starts fresh, no hidden progress");
    }
    // Concurrent reloads and choices serialize state and keep configuration generations monotonic.
    vr::Selector shared(343); std::mutex collected_mutex; std::vector<vr::Selection> collected;
    std::thread reload([&] {for(unsigned i=0;i<500;++i) shared.configure_rules({bool(i&1),bool(i&2),bool(i&4)});});
    std::vector<std::thread> workers;
    for(unsigned i=0;i<4;++i) workers.emplace_back([&] {
        for(unsigned j=0;j<250;++j) {
            auto x=shared.choose("concurrent",vr::Mode::Random,50);
            std::lock_guard<std::mutex> guard(collected_mutex); collected.push_back(x);
        }
    });
    reload.join(); for(auto& w:workers) w.join();
    std::sort(collected.begin(),collected.end(),[](auto& a,auto& b){return a.sequence<b.sequence;});
    for(size_t i=0;i<collected.size();++i) {
        const auto& x=collected[i];
        check(x.sequence==i+1,"concurrent config changes keep event ordering");
        if(i && x.rules_epoch==collected[i-1].rules_epoch)
            check(x.rules==collected[i-1].rules && x.count_before==collected[i-1].count_after && x.guarantee_before==collected[i-1].guarantee_after,"one epoch has consistent config and state");
        else check(x.count_before==0 && !x.guarantee_before,"new epoch begins with zero state");
        if(i) check(x.rules_epoch>=collected[i-1].rules_epoch,"rule epochs never move backward");
        if(!x.rules.radiance) check(x.count_before==0 && x.count_after==0,"disabled radiance never accumulates");
        if(!x.rules.guarantee) check(!x.guarantee_before && !x.guarantee_after,"disabled guarantee never accumulates");
    }
    std::cout<<"PASS: both targets x four switch combinations; mirrored badges, independent counters, reload resets, 1000 choices with concurrent rule changes\n";
}

void test_ini_config(const std::filesystem::path& root) {
    const auto folder=root/L"voice_randomizer/build/config_tests";
    std::filesystem::create_directories(folder);
    const auto path=folder/L"bilingual.ini";
    std::filesystem::copy_file(root/L"voice_randomizer/release/voice_randomizer.ini",path,std::filesystem::copy_options::overwrite_existing);
    ini_path=path.wstring();
    selector=std::make_unique<vr::Selector>(42,[]{return 0u;});
    read_config();
    check(mode.load()==vr::Mode::Random && percent.load()==50 && enabled.load() && !log_cache.load(),"Windows profile API parses shipped bilingual INI defaults");
    auto first=selector->choose("first",mode.load(),percent.load());
    check(first.rules==vr::Rules{} && first.guarantee_after && first.count_after==1,"shipped rule defaults preserve v1.0 behavior");
    read_config();
    check(selector->choose("second",mode.load(),percent.load()).reason==vr::Reason::Guarantee,"unchanged reload preserves guarantee");
    auto write=[&](const wchar_t* key,const wchar_t* value) {
        check(WritePrivateProfileStringW(L"Randomizer",key,value,ini_path.c_str())!=0,"write real INI setting");
    };
    write(L"GuaranteeTarget",L"OLD"); write(L"GuaranteeEnabled",L"0"); write(L"CapturingRadianceEnabled",L"0");
    write(L"OldPercent",L"-20"); read_config();
    auto x=selector->choose("third",mode.load(),percent.load());
    check(percent.load()==0 && x.rules==vr::Rules{true,false,false} && x.count_before==0 && !x.guarantee_before,"INI target/switch reload and negative percent clamping");
    write(L"OldPercent",L"140"); read_config(); check(percent.load()==100,"upper bound clamping");
    write(L"OldPercent",L"invalid"); write(L"Mode",L"invalid"); write(L"GuaranteeTarget",L"invalid");
    write(L"GuaranteeEnabled",L"invalid"); write(L"CapturingRadianceEnabled",L"invalid"); read_config();
    x=selector->choose("fourth",mode.load(),percent.load());
    check(percent.load()==50 && mode.load()==vr::Mode::Random && x.rules==vr::Rules{},"invalid values fall back to documented defaults");
    // Actual hook log fixture, audited in Python. This does not launch the game.
    _wfopen_s(&log_file,(folder/L"hook_rules.log").c_str(),L"w");
    check(log_file!=nullptr,"open integration log");
    log("INIT Voice Randomizer 1.1.0; synthetic hook integration test");
    log("READY synthetic=1");
    selector=std::make_unique<vr::Selector>(42,[]{return 50u;});
    original_play=fake_play;
    const auto asset_path=root/L"voice_randomizer/assets/old/v006_b0001.wav";
    assets.emplace("v006_b0001",Asset{1910,"vr_old_v006_b0001","test-old.wav",asset_path.wstring()});
    vr::PlayRequest req{}; req.category=2; req.name="v006_b0001";
    for(bool target : {false,true}) for(bool g : {false,true}) for(bool r : {false,true}) {
        write(L"GuaranteeTarget",target?L"old":L"new"); write(L"GuaranteeEnabled",g?L"1":L"0");
        write(L"CapturingRadianceEnabled",r?L"1":L"0"); write(L"OldPercent",target?L"0":L"100"); read_config();
        for(unsigned i=0;i<8;++i) play_hook(nullptr,&req);
        const auto rows=vr::ui::history().snapshot();
        const auto& first_row=rows[rows.size()-8];
        check(first_row.old!=target && first_row.badge==vr::ui::Badge::Wai,"actual hook uses mirrored avatar and miss badge");
        const auto& fourth_draw=rows[rows.size()-8+(g?6:3)];
        if(r) check(fourth_draw.old==target && fourth_draw.badge==vr::ui::Badge::Radiance,"actual hook uses target avatar for radiance");
    }
    fclose(log_file); log_file=nullptr; assets.clear(); requested.clear(); fake_cache.clear();
    ini_path=(root/L"voice_randomizer/release/voice_randomizer.ini").wstring(); read_config();
    std::cout<<"PASS: real Windows INI parsing/reload, UTF-16 Chinese comments, invalid values, hook routing and badges across all configurations\n";
}
