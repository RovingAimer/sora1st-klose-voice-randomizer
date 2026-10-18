#include "editor_config.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <atomic>
using namespace editor;
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
void write(const std::filesystem::path& p,const std::vector<unsigned char>& bytes){std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());check(bool(f),"test fixture write");}
void profile_matches(const std::filesystem::path& p,const Values& v){
    for(size_t i=0;i<specs.size();++i){wchar_t text[256]{};GetPrivateProfileStringW(specs[i].section,specs[i].key,L"MISSING",text,256,p.c_str());check(text==v[i],"real Windows Profile API reads saved fields");}
}
int wmain(int argc,wchar_t** argv){try{
    check(argc==2,"game root argument required");
    const std::filesystem::path root(argv[1]);
    const auto folder=root/L"voice_randomizer/build"/(L"editor-tests-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(folder/L"含中文和空格");
    const auto path=folder/L"含中文和空格/voice_randomizer.ini";
    const auto release=editor::read(root/L"voice_randomizer/release/voice_randomizer.ini",L"");
    check(release.values==defaults(),"release config agrees with editor defaults");
    std::wstring text=release.text+L"\r\n; 保留用户自己的注释\r\n[Unrelated]\r\nCustom=hello\r\n";
    write(path,encode(text));
    auto doc=editor::read(path,L"");auto values=doc.values;
    values[Enabled]=L"0";values[Mode]=L"alternate";values[OldPercent]=L"83";values[Target]=L"old";
    values[Guarantee]=L"0";values[Radiance]=L"0";values[AssetSet]=L"placeholder";values[LogCache]=L"1";
    values[OverlayEnabled]=L"0";values[Left]=L"90";values[Center]=L"100";values[Height]=L"10";values[Scroll]=L"2000";
    std::cout<<"Checking round-trip..."<<std::endl;
    auto saved=editor::save(path,doc,values);profile_matches(path,values);
    check(saved.bytes[0]==0xff && saved.bytes[1]==0xfe,"save keeps UTF-16 LE BOM");
    check(saved.text.find(L"; 保留用户自己的注释")!=std::wstring::npos && saved.text.find(L"Custom=hello")!=std::wstring::npos,"comments and unknown sections preserved");
    for(const auto& line:split(release.text,L'\n'))if(!line.empty() && line[0]==L';')check(saved.text.find(line)!=std::wstring::npos,"every original bilingual comment preserved");
    const auto original=saved.bytes;
    write(path,encode(saved.text+L"; external change\r\n"));
    bool conflict=false;try{editor::save(path,saved,defaults());}catch(const Error& e){conflict=e.kind==Failure::Conflict;}
    check(conflict && editor::read(path,L"").text.find(L"external change")!=std::wstring::npos,"external change is detected and retained");
    doc=editor::read(path,L"");
    SetFileAttributesW(path.c_str(),FILE_ATTRIBUTE_READONLY);
    bool denied=false;try{editor::save(path,doc,defaults());}catch(const Error&){denied=true;}
    SetFileAttributesW(path.c_str(),FILE_ATTRIBUTE_NORMAL);
    check(denied && editor::read(path,L"").bytes==doc.bytes,"read-only save fails without damaging original");
    auto invalid=defaults();invalid[OldPercent]=L"101";
    bool rejected=false;try{editor::save(path,doc,invalid);}catch(const Error& e){rejected=e.kind==Failure::Invalid;}
    check(rejected && editor::read(path,L"").bytes==doc.bytes,"invalid UI range cannot be saved");
    std::cout<<"Checking creation/import..."<<std::endl;
    const auto missing=folder/L"new.ini";
    const auto fresh=editor::read(missing,release.text);
    check(!fresh.exists,"missing config opens as unsaved defaults");
    auto created=editor::save(missing,fresh,defaults());profile_matches(missing,defaults());
    check(created.text==release.text,"missing config retains complete embedded template");
    const auto utf8=folder/L"utf8.ini";
    std::string bytes=u8"; 中文 UTF-8 注释\n[Randomizer]\nMode=OLD\nOldPercent=-20\n[Overlay]\nHeightPercent=999\n[Keep]\nKey=value\n";
    write(utf8,std::vector<unsigned char>(bytes.begin(),bytes.end()));
    auto partial=editor::read(utf8,L"");
    check(partial.values[Mode]==L"old" && partial.values[OldPercent]==L"0" && partial.values[Height]==L"100","input normalisation matches runtime ranges and case folding");
    auto repaired=editor::save(utf8,partial,partial.values);profile_matches(utf8,partial.values);
    check(repaired.text.find(L"中文 UTF-8 注释")!=std::wstring::npos && repaired.text.find(L"Key=value")!=std::wstring::npos,"UTF-8 conversion and missing-key insertion retain text");
    const auto bad=folder/L"malformed.ini";write(bad,{0xff,0xfe,0x01});
    bool encoding=false;try{editor::read(bad,L"");}catch(const Error& e){encoding=e.kind==Failure::Encoding;}
    check(encoding && std::filesystem::file_size(bad)==3,"malformed UTF-16 is rejected without rewriting");
    // Concurrent reads must see a complete old or new configuration, never a partial write.
    std::cout<<"Checking concurrent reads..."<<std::endl;
    std::atomic<bool> done=false,failed=false;std::atomic<unsigned> reads=0;
    std::thread reader([&]{try{while(!done){auto d=editor::read(missing,L"");if(!d.exists || d.text.find(L"ScrollMs=")==std::wstring::npos)failed=true;++reads;}}catch(const Error& e){std::cerr<<"Concurrent read error="<<e.code<<'\n';failed=true;}catch(...){failed=true;}});
    try{for(unsigned i=0;i<40;++i){auto v=created.values;v[OldPercent]=std::to_wstring(i);created=editor::save(missing,created,v);}}
    catch(...){done=true;reader.join();throw;}
    done=true;reader.join();check(!failed && reads>0,"concurrent readers observe complete configs throughout atomic saves");
    for(const auto& item:std::filesystem::directory_iterator(path.parent_path()))check(item.path().extension()==L".ini","failed saves clean temporary and lock files");
    std::cout<<"PASS: 13 options via Windows Profile API, Unicode paths, exact comment/unknown-key preservation, conflict and read-only protection\n";
    std::cout<<"PASS: invalid value rejection, missing-file template, UTF-8 import, malformed encoding, atomic save with "<<reads<<" concurrent reads\nALL EDITOR CONFIG TESTS PASSED\n";
    return 0;
}catch(const Error& e){std::cerr<<"FAIL: "<<e.what()<<" kind="<<static_cast<int>(e.kind)<<" Windows error="<<e.code<<'\n';return 1;}
catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
