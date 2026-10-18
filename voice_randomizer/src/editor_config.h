#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace editor {
enum Field { Enabled, Mode, OldPercent, Target, Guarantee, Radiance, AssetSet, LogCache,
             OverlayEnabled, Left, Center, Height, Scroll, FieldCount };
struct Spec { const wchar_t* section; const wchar_t* key; const wchar_t* initial; int low; int high; const wchar_t* choices; };
inline constexpr std::array<Spec,FieldCount> specs{{
    {L"Randomizer",L"Enabled",L"1",0,1,nullptr},
    {L"Randomizer",L"Mode",L"random",0,0,L"random|new|old|alternate"},
    {L"Randomizer",L"OldPercent",L"50",0,100,nullptr},
    {L"Randomizer",L"GuaranteeTarget",L"new",0,0,L"new|old"},
    {L"Randomizer",L"GuaranteeEnabled",L"1",0,1,nullptr},
    {L"Randomizer",L"CapturingRadianceEnabled",L"1",0,1,nullptr},
    {L"Randomizer",L"ReplacementSet",L"legacy",0,0,L"legacy|placeholder"},
    {L"Randomizer",L"LogCache",L"0",0,1,nullptr},
    {L"Overlay",L"Enabled",L"1",0,1,nullptr},
    {L"Overlay",L"LeftPercent",L"2",0,90,nullptr},
    {L"Overlay",L"CenterYPercent",L"40",0,100,nullptr},
    {L"Overlay",L"HeightPercent",L"50",10,100,nullptr},
    {L"Overlay",L"ScrollMs",L"180",0,2000,nullptr},
}};
using Values=std::array<std::wstring,FieldCount>;
inline Values defaults() { Values v; for(size_t i=0;i<v.size();++i) v[i]=specs[i].initial; return v; }
enum class Failure { IO, Encoding, Conflict, Invalid };
struct Error : std::runtime_error {
    Failure kind; DWORD code;
    explicit Error(Failure k,DWORD c=0,const char* operation="settings file operation failed"):std::runtime_error(operation),kind(k),code(c){}
};
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h):value(h){}
    ~Handle(){ if(value!=INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&)=delete;
    void close(){ if(value!=INVALID_HANDLE_VALUE){CloseHandle(value); value=INVALID_HANDLE_VALUE;} }
};
inline std::wstring trim(std::wstring s) {
    const auto first=s.find_first_not_of(L" \t\r\n");
    if(first==std::wstring::npos) return {};
    return s.substr(first,s.find_last_not_of(L" \t\r\n")-first+1);
}
inline bool equal(const std::wstring& a,const wchar_t* b) { return _wcsicmp(a.c_str(),b)==0; }
inline std::vector<std::wstring> split(const std::wstring& s,wchar_t separator) {
    std::vector<std::wstring> result; size_t pos=0;
    while(pos<s.size()) { const auto end=s.find(separator,pos); result.push_back(s.substr(pos,end-pos)); if(end==std::wstring::npos) break; pos=end+1; }
    return result;
}
inline bool integer(const std::wstring& text,int low,int high,int& result,bool clamp=false) {
    const auto s=trim(text); wchar_t* end=nullptr; errno=0;
    long number=wcstol(s.c_str(),&end,10);
    if(end==s.c_str() || *end || errno==ERANGE) return false;
    if(!clamp && (number<low || number>high)) return false;
    result=static_cast<int>(std::clamp(number,static_cast<long>(low),static_cast<long>(high))); return true;
}
inline std::wstring normalise(size_t index,std::wstring s) {
    const auto& spec=specs[index]; s=trim(s);
    if(s.size()>=2 && ((s.front()==L'"' && s.back()==L'"') || (s.front()==L'\'' && s.back()==L'\''))) s=s.substr(1,s.size()-2);
    if(spec.choices) { for(const auto& v:split(spec.choices,L'|')) if(equal(s,v.c_str())) return v; }
    else { int n=0; if(integer(s,spec.low,spec.high,n,true)) return std::to_wstring(n); }
    return spec.initial;
}
inline std::wstring decode(const std::vector<unsigned char>& bytes) {
    if(bytes.size()>=2 && ((bytes[0]==0xff && bytes[1]==0xfe) || (bytes[0]==0xfe && bytes[1]==0xff))) {
        if(bytes.size()%2) throw Error(Failure::Encoding);
        std::wstring s; const bool le=bytes[0]==0xff;
        for(size_t i=2;i<bytes.size();i+=2) s+=static_cast<wchar_t>(le?(bytes[i]|bytes[i+1]<<8):(bytes[i]<<8|bytes[i+1]));
        if(s.find(L'\0')!=std::wstring::npos) throw Error(Failure::Encoding);
        return s;
    }
    const size_t offset=bytes.size()>=3 && bytes[0]==0xef && bytes[1]==0xbb && bytes[2]==0xbf?3:0;
    if(offset==bytes.size()) return {};
    const char* src=reinterpret_cast<const char*>(bytes.data()+offset);
    const int count=static_cast<int>(bytes.size()-offset);
    int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,src,count,nullptr,0);
    if(!length) throw Error(Failure::Encoding);
    std::wstring s(length,L'\0'); MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,src,count,s.data(),length);
    if(s.find(L'\0')!=std::wstring::npos) throw Error(Failure::Encoding);
    return s;
}
inline std::vector<unsigned char> encode(const std::wstring& text) {
    std::vector<unsigned char> data{0xff,0xfe}; data.resize(2+text.size()*2);
    memcpy(data.data()+2,text.data(),text.size()*2); return data;
}
struct Document { bool exists=false; std::vector<unsigned char> bytes; std::wstring text; Values values=defaults(); bool normalised=false; };
inline Document read(const std::filesystem::path& path,const std::wstring& fallback) {
    Document d;
    HANDLE opened=INVALID_HANDLE_VALUE;
    for(int attempt=0;attempt<10;++attempt){
        opened=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(opened!=INVALID_HANDLE_VALUE || (GetLastError()!=ERROR_SHARING_VIOLATION && GetLastError()!=ERROR_ACCESS_DENIED) || attempt==9)break;
        Sleep(10);
    }
    Handle file(opened);
    if(file.value==INVALID_HANDLE_VALUE) {
        const DWORD code=GetLastError();
        if(code!=ERROR_FILE_NOT_FOUND) throw Error(Failure::IO,code);
        d.text=fallback;
    } else {
        d.exists=true; LARGE_INTEGER size{};
        if(!GetFileSizeEx(file.value,&size) || size.QuadPart<0 || size.QuadPart>2*1024*1024) throw Error(Failure::IO,ERROR_FILE_TOO_LARGE);
        d.bytes.resize(static_cast<size_t>(size.QuadPart)); DWORD got=0;
        if(!ReadFile(file.value,d.bytes.data(),static_cast<DWORD>(d.bytes.size()),&got,nullptr) || got!=d.bytes.size()) throw Error(Failure::IO,GetLastError());
        d.text=decode(d.bytes);
    }
    std::array<bool,FieldCount> seen{}; std::wstring section;
    for(const auto& line:split(d.text,L'\n')) {
        const auto t=trim(line);
        if(t.empty() || t[0]==L';' || t[0]==L'#') continue;
        if(t[0]==L'[' && t.back()==L']') { section=trim(t.substr(1,t.size()-2)); continue; }
        const auto at=t.find(L'='); if(at==std::wstring::npos) continue;
        for(size_t i=0;i<specs.size();++i) if(!seen[i] && equal(section,specs[i].section) && equal(trim(t.substr(0,at)),specs[i].key)) {
            const auto raw=trim(t.substr(at+1)); d.values[i]=normalise(i,raw); seen[i]=true;
            if(!equal(raw,d.values[i].c_str())) d.normalised=true;
        }
    }
    return d;
}
inline std::wstring updated_text(const Document& d,const Values& v) {
    auto lines=split(d.text,L'\n');
    for(auto& line:lines) if(!line.empty() && line.back()==L'\r') line.pop_back();
    for(size_t i=0;i<specs.size();++i) {
        const auto& spec=specs[i];
        if(v[i]!=normalise(i,v[i])) throw Error(Failure::Invalid);
        std::wstring section; bool found=false; size_t insert=lines.size(); bool has_section=false;
        for(size_t n=0;n<lines.size();++n) {
            const auto t=trim(lines[n]);
            if(t.empty() || t[0]==L';' || t[0]==L'#') continue;
            if(t[0]==L'[' && t.back()==L']') {
                if(equal(section,spec.section) && !has_section){ insert=n; has_section=true; }
                section=trim(t.substr(1,t.size()-2)); continue;
            }
            const auto eq=lines[n].find(L'=');
            if(equal(section,spec.section) && eq!=std::wstring::npos && equal(trim(lines[n].substr(0,eq)),spec.key)) {
                lines[n]=lines[n].substr(0,eq+1)+v[i]; found=true;
            }
        }
        has_section=has_section || equal(section,spec.section);
        if(!found) {
            if(!has_section){lines.push_back(L""); lines.push_back(std::wstring(L"[")+spec.section+L"]"); insert=lines.size();}
            lines.insert(lines.begin()+insert,std::wstring(spec.key)+L"="+v[i]);
        }
    }
    std::wstring result; for(const auto& line:lines) result+=line+L"\r\n"; return result;
}
inline Document save(const std::filesystem::path& path,const Document& expected,const Values& values) {
    // Serialize saves by this editor, then check for external edits before replacing the file.
    const auto lock_path=path.wstring()+L".editor-lock";
    Handle lock(CreateFileW(lock_path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr));
    if(lock.value==INVALID_HANDLE_VALUE) throw Error(Failure::IO,GetLastError(),"lock configuration");
    auto current=read(path,L"");
    if(current.exists!=expected.exists || current.bytes!=expected.bytes) throw Error(Failure::Conflict);
    auto bytes=encode(updated_text(expected,values));
    const auto temp=path.wstring()+L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    Handle output(CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(output.value==INVALID_HANDLE_VALUE) throw Error(Failure::IO,GetLastError(),"create temporary configuration");
    try {
        DWORD written=0;
        if(!WriteFile(output.value,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) || written!=bytes.size() || !FlushFileBuffers(output.value)) throw Error(Failure::IO,GetLastError());
        output.close();
        current=read(path,L"");
        if(current.exists!=expected.exists || current.bytes!=expected.bytes) throw Error(Failure::Conflict);
        // Same-directory rename avoids ReplaceFileW's metadata merge and its extra file opens.
        bool replaced=false;DWORD error=0;
        for(int attempt=0;attempt<20;++attempt){
            if(MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH|(expected.exists?MOVEFILE_REPLACE_EXISTING:0))){replaced=true;break;}
            error=GetLastError();
            if(error!=ERROR_SHARING_VIOLATION && error!=ERROR_ACCESS_DENIED)break;
            Sleep(10);
            current=read(path,L"");
            if(current.exists!=expected.exists || current.bytes!=expected.bytes)throw Error(Failure::Conflict);
        }
        if(!replaced) throw Error(Failure::IO,error,"replace configuration");
    } catch(...) { output.close(); DeleteFileW(temp.c_str()); throw; }
    return read(path,L"");
}
} // namespace editor
