#include "editor_config.h"
#include <commctrl.h>
#include <shellapi.h>
#include <memory>
#pragma comment(lib,"comctl32.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {
using namespace editor;
constexpr int LanguageId=30,TabId=31,SaveId=32,DefaultsId=33,ReloadId=34;
struct Control { HWND hwnd; int page,x,y,w,h; };
struct App {
    HWND window=nullptr, tabs=nullptr, status=nullptr, rule_text=nullptr, preview=nullptr;
    std::array<HWND,FieldCount> fields{};
    std::vector<Control> controls;
    std::filesystem::path path;
    std::wstring fallback;
    Document document;
    Values view=defaults();
    HFONT font=nullptr, heading=nullptr;
    HBRUSH white=CreateSolidBrush(RGB(255,255,255));
    UINT dpi=96; int page=0; bool english=false,building=false,dirty=false;
    const wchar_t* tr(const wchar_t* zh,const wchar_t* en) const {return english?en:zh;}
    int px(int n) const {return MulDiv(n,static_cast<int>(dpi),96);}
    ~App(){if(font)DeleteObject(font);if(heading)DeleteObject(heading);DeleteObject(white);}
    void fonts() {
        if(font)DeleteObject(font);if(heading)DeleteObject(heading);
        font=CreateFontW(-px(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        heading=CreateFontW(-px(23),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    }
    HWND add(const wchar_t* cls,const wchar_t* title,DWORD style,int id,int p,int x,int y,int w,int h,DWORD ex=0) {
        HWND control=CreateWindowExW(ex,cls,title,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(w),px(h),window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        if(!control)throw Error(Failure::IO,GetLastError());
        SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        controls.push_back({control,p,x,y,w,h}); return control;
    }
    HWND label(const wchar_t* zh,const wchar_t* en,int p,int x,int y,int w,int h) {return add(L"STATIC",tr(zh,en),SS_LEFT,0,p,x,y,w,h);}
    void check(Field id,const wchar_t* zh,const wchar_t* en,int p,int x,int y,int w) {
        fields[id]=add(L"BUTTON",tr(zh,en),BS_AUTOCHECKBOX|WS_TABSTOP,100+id,p,x,y,w,28);
        SendMessageW(fields[id],BM_SETCHECK,view[id]==L"1"?BST_CHECKED:BST_UNCHECKED,0);
    }
    void number(Field id,int p,int x,int y,int w=98) {
        fields[id]=add(L"EDIT",view[id].c_str(),ES_AUTOHSCROLL|WS_TABSTOP,100+id,p,x,y,w,29,WS_EX_CLIENTEDGE);
        SendMessageW(fields[id],EM_SETLIMITTEXT,8,0);
        HWND spin=add(UPDOWN_CLASSW,L"",UDS_ALIGNRIGHT|UDS_ARROWKEYS|UDS_SETBUDDYINT|UDS_NOTHOUSANDS,200+id,p,x+w-20,y,20,29);
        SendMessageW(spin,UDM_SETBUDDY,reinterpret_cast<WPARAM>(fields[id]),0);
        SendMessageW(spin,UDM_SETRANGE32,specs[id].low,specs[id].high);
        int n=0;if(integer(view[id],specs[id].low,specs[id].high,n))SendMessageW(spin,UDM_SETPOS32,0,n);
        SetWindowTextW(fields[id],view[id].c_str());
    }
    void combo(Field id,const std::vector<const wchar_t*>& items,int p,int x,int y,int w) {
        fields[id]=add(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,100+id,p,x,y,w,220);
        for(auto item:items)SendMessageW(fields[id],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(item));
        auto choices=split(specs[id].choices,L'|'); int selected=0;
        for(size_t i=0;i<choices.size();++i)if(view[id]==choices[i])selected=static_cast<int>(i);
        SendMessageW(fields[id],CB_SETCURSEL,selected,0);
    }
    Values collect() const {
        Values result;
        for(size_t i=0;i<fields.size();++i) {
            if(specs[i].choices) {
                const auto options=split(specs[i].choices,L'|'); const LRESULT n=SendMessageW(fields[i],CB_GETCURSEL,0,0);
                result[i]=n>=0 && static_cast<size_t>(n)<options.size()?options[static_cast<size_t>(n)]:specs[i].initial;
            } else if(specs[i].high==1)result[i]=SendMessageW(fields[i],BM_GETCHECK,0,0)==BST_CHECKED?L"1":L"0";
            else {wchar_t text[64]{};GetWindowTextW(fields[i],text,64);result[i]=trim(text);}
        }
        return result;
    }
    void show_page() {
        for(const auto& c:controls)ShowWindow(c.hwnd,c.page<0 || c.page==page?SW_SHOW:SW_HIDE);
        InvalidateRect(window,nullptr,TRUE);
    }
    void set_status(const wchar_t* zh,const wchar_t* en){SetWindowTextW(status,tr(zh,en));}
    void update() {
        if(building)return;
        view=collect();dirty=view!=document.values || !document.exists || document.normalised;
        EnableWindow(GetDlgItem(window,SaveId),dirty);
        const bool random=view[Mode]==L"random";
        for(auto id:{OldPercent,Target,Guarantee,Radiance,SoftPity})EnableWindow(fields[id],random);
        EnableWindow(GetDlgItem(window,200+OldPercent),random);
        for(auto id:{SoftStart,SoftStep}) {
            EnableWindow(fields[id],random && view[SoftPity]==L"1");
            EnableWindow(GetDlgItem(window,200+id),random && view[SoftPity]==L"1");
        }
        if(!random)SetWindowTextW(rule_text,tr(L"当前模式暂停所有保底计数；交替从新版开始。\r\n缺少旧资源的语音使用游戏配音。",L"This mode pauses all pity counters. Alternation starts with new.\r\nLines without legacy resources use game audio."));
        else SetWindowTextW(rule_text,tr(L"软保底：连续未中目标 X 次后，下次提升所设百分点；再未中继续加。\r\n命中目标（包括硬保底）清零；目标结果最高为 100%。",L"Soft pity: after X consecutive misses, add the set points per further draw.\r\nAny target hit (including Guarantee) resets it. Target chance caps at 100%."));
        if(document.normalised && view==document.values)set_status(L"部分原有设置需要更新；点击保存可保留有效值并清理旧选项。",L"Some old settings need updating. Save to keep valid values and remove retired options.");
        else if(dirty)set_status(L"有尚未保存的更改。",L"You have unsaved changes.");
        else set_status(L"配置已读取。保存后，多数设置约 1 秒在游戏中生效。",L"Settings loaded. Most changes take effect in-game about one second after saving.");
        if(preview)InvalidateRect(preview,nullptr,TRUE);
    }
    void build() {
        building=true;
        for(const auto& c:controls)DestroyWindow(c.hwnd);controls.clear();fields.fill(nullptr);preview=nullptr;
        fonts();
        SetWindowTextW(window,tr(L"克萝赛随机语音 MOD · 设置",L"Klose Voice Mod - Settings"));
        HWND title=label(L"克萝赛 · MOD 设置",L"Klose Voice Mod Settings",-1,22,16,525,32);
        SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(heading),TRUE);
        label(L"调整语音选择和游戏内播放记录。",L"Choose your voices and adjust the in-game history.",-1,22,53,658,25);
        HWND language=add(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP,LanguageId,-1,564,19,134,160);
        SendMessageW(language,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"简体中文"));SendMessageW(language,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"English"));SendMessageW(language,CB_SETCURSEL,english?1:0,0);
        tabs=add(WC_TABCONTROLW,L"",WS_TABSTOP|WS_CLIPSIBLINGS,TabId,-1,20,87,680,443);
        const wchar_t* names[]{tr(L"语音与随机",L"Voices & Random"),tr(L"播放记录",L"History"),tr(L"高级",L"Advanced")};
        for(int i=0;i<3;++i){TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=const_cast<wchar_t*>(names[i]);TabCtrl_InsertItem(tabs,i,&item);}TabCtrl_SetCurSel(tabs,page);
        add(L"STATIC",L"",SS_WHITERECT,46,-1,22,119,676,409);
        check(Enabled,L"启用语音替换",L"Enable voice replacement",0,40,128,282);
        label(L"默认音量 (%)",L"Default volume (%)",0,350,136,194,25);number(DefaultVolume,0,554,128,116);
        label(L"播放模式",L"Playback mode",0,40,168,165,25);
        combo(Mode,{tr(L"随机选择",L"Random"),tr(L"始终新版",L"Always new"),tr(L"始终旧版",L"Always legacy"),tr(L"同一句新旧交替",L"Alternate per line")},0,230,160,440);
        label(L"基础旧版概率 (%)",L"Base OLD chance (%)",0,40,210,186,25);number(OldPercent,0,230,202);
        label(L"保底目标",L"Preferred voice",0,367,210,143,25);combo(Target,{tr(L"新版",L"New"),tr(L"旧版",L"Legacy")},0,518,202,152);
        check(Guarantee,L"启用硬保底",L"Enable Guarantee",0,40,240,282);
        check(Radiance,L"启用捕获明光",L"Enable Capturing Radiance",0,350,240,320);
        check(SoftPity,L"启用软保底",L"Enable soft pity",0,40,274,630);
        label(L"连续未中次数 X",L"Consecutive misses X",0,40,312,186,25);number(SoftStart,0,230,304);
        label(L"每次提升百分点",L"Added points per draw",0,350,312,194,25);number(SoftStep,0,554,304,116);
        rule_text=label(L"",L"",0,40,344,630,49);
        label(L"默认音量适用于新版、旧版及随机/交替模式。\r\n音量 0 静音、100 原音量；保存后对后续语音生效。",L"Default volume applies to new, legacy, random and alternate modes.\r\nVolume 0 mutes; 100 is unchanged. Saved changes apply to later voices.",0,40,404,630,49);
        label(L"目标/硬保底/明光变化清全部计数，软参数只清软计数。",L"Target/Guarantee/Radiance reset all; soft settings reset only soft pity.",0,40,478,630,43);
        check(OverlayEnabled,L"显示游戏内播放记录",L"Show in-game voice history",1,40,128,630);
        check(HideOther,L"下一条非克萝赛语音时隐藏，克萝赛语音时恢复",L"Hide on other voices; show again when Klose speaks",1,40,162,630);
        label(L"左边距 (%)",L"Left margin (%)",1,40,210,170,25);number(Left,1,220,202);
        label(L"垂直中心 (%)",L"Vertical center (%)",1,370,210,178,25);number(Center,1,554,202,116);
        label(L"UI 区域高度 (%)",L"UI region height (%)",1,40,252,170,25);number(Height,1,220,244);
        label(L"动画时长 (毫秒)",L"Animation (ms)",1,370,252,178,25);number(Scroll,1,554,244,116);
        label(L"显示条数",L"Visible entries",1,40,294,170,25);number(MaxEntries,1,220,286);
        label(L"常态透明度 (%)",L"Transparency (%)",1,370,294,178,25);number(Transparency,1,554,286,116);
        label(L"歪图标缩放 (%)",L"Wai badge scale (%)",1,40,336,170,25);number(WaiScale,1,220,328);
        label(L"透明度 100 = 完全隐藏",L"100% transparency = hidden",1,370,336,300,25);
        preview=add(L"STATIC",L"",SS_OWNERDRAW,40,1,40,378,330,132);
        label(L"位置预览（示意）\r\n最新记录从底部出现。\r\n条数只控制数量；区域高度决定大小。\r\n歪图标以自身左上角固定缩放；\r\n不影响头像和其他标识。\r\n缩放 0 隐藏歪图标。",L"Position preview (schematic)\r\nNewest entries appear below.\r\nCount controls quantity, not icon size.\r\nWai scales from its own top-left;\r\navatars and other badges stay fixed.\r\n0% scale hides Wai.",1,394,378,276,139);
        label(L"旧版分支素材",L"Legacy-branch audio",2,40,151,199,25);
        combo(AssetSet,{tr(L"正式旧版语音",L"Real legacy voices"),tr(L"测试占位音频（仅开发版）",L"Test placeholders (development only)")},2,250,141,420);
        label(L"正式发布包请选择旧版语音。\r\n切换素材需要重启游戏；测试音频仅在资源存在时可选用。",L"Use real legacy voices with the release package.\r\nSwitching sets requires a game restart. Test audio requires the separate development assets.",2,40,193,630,63);
        check(LogCache,L"记录详细缓存日志（排查问题时使用）",L"Log cache details (for troubleshooting)",2,40,279,630);
        label(L"关闭后仍记录播放选择和错误。",L"Selections and errors are still logged when this is off.",2,40,319,630,40);
        label(L"当前配置文件",L"Current settings file",2,40,377,630,25);
        add(L"EDIT",path.c_str(),ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_TABSTOP,45,2,40,410,630,56,WS_EX_CLIENTEDGE);
        label(L"保存保留 INI 注释；恢复默认后仍需点击保存。",L"Saving keeps INI comments. Restored defaults are applied only after saving.",2,40,485,630,32);
        status=label(L"",L"",-1,22,547,676,36);
        add(L"BUTTON",tr(L"恢复默认",L"Restore defaults"),BS_PUSHBUTTON|WS_TABSTOP,DefaultsId,-1,22,593,155,32);
        add(L"BUTTON",tr(L"重新读取",L"Reload"),BS_PUSHBUTTON|WS_TABSTOP,ReloadId,-1,190,593,137,32);
        add(L"BUTTON",tr(L"保存设置",L"Save settings"),BS_DEFPUSHBUTTON|WS_TABSTOP,SaveId,-1,535,593,163,32);
        building=false;show_page();update();
    }
    void error(const Error& e) {
        std::wstring message;
        if(e.kind==Failure::Conflict)message=tr(L"配置文件已被其他窗口修改。请点击“重新读取”，再进行修改和保存。",L"Another window changed this file. Reload before making and saving your changes.");
        else if(e.kind==Failure::Encoding)message=tr(L"无法读取配置文件的编码。请使用 UTF-16 LE 或 UTF-8 文件；原文件未被改写。",L"Cannot read this file's encoding. Use UTF-16 LE or UTF-8. The original file has not been changed.");
        else message=tr(L"无法读取或保存配置。请检查目录的写入权限，以及文件是否被其他程序占用。",L"Cannot read or save settings. Check folder permissions and whether another program is locking the file.");
        if(e.code)message+=L"\r\nWindows error: "+std::to_wstring(e.code);
        MessageBoxW(window,message.c_str(),tr(L"配置未保存",L"Settings not saved"),MB_OK|MB_ICONWARNING);
    }
    bool save_settings() {
        view=collect();
        for(size_t i=0;i<specs.size();++i)if(!specs[i].choices) {
            int n=0;if(!integer(view[i],specs[i].low,specs[i].high,n)) {
                switch(static_cast<Field>(i)) {
                case OverlayEnabled:case Left:case Center:case Height:case Scroll:
                case MaxEntries:case Transparency:case WaiScale:case HideOther:page=1;break;
                case AssetSet:case LogCache:page=2;break;
                default:page=0;break;
                }
                TabCtrl_SetCurSel(tabs,page);show_page();
                const auto range=std::to_wstring(specs[i].low)+L" – "+std::to_wstring(specs[i].high);
                const auto message=std::wstring(tr(L"请输入范围内的整数：",L"Enter an integer in this range: "))+range;
                MessageBoxW(window,message.c_str(),tr(L"数值无效",L"Invalid value"),MB_OK|MB_ICONWARNING);
                SetFocus(fields[i]);SendMessageW(fields[i],EM_SETSEL,0,-1);return false;
            }
            view[i]=std::to_wstring(n);
        }
        if(view[AssetSet]==L"placeholder" && !std::filesystem::is_directory(path.parent_path()/L"assets/placeholder")) {
            page=2;TabCtrl_SetCurSel(tabs,page);show_page();SetFocus(fields[AssetSet]);
            MessageBoxW(window,tr(L"此目录没有测试占位音频。请选择“正式旧版语音”。",L"This folder has no test audio. Choose real legacy voices."),tr(L"缺少测试资源",L"Test assets missing"),MB_OK|MB_ICONWARNING);return false;
        }
        try {
            const bool restart=view[AssetSet]!=document.values[AssetSet];
            const bool reset=view[Target]!=document.values[Target] || view[Guarantee]!=document.values[Guarantee] || view[Radiance]!=document.values[Radiance];
            const bool soft_reset=view[SoftPity]!=document.values[SoftPity] || view[SoftStart]!=document.values[SoftStart] || view[SoftStep]!=document.values[SoftStep];
            document=editor::save(path,document,view);build();
            if(restart)set_status(L"已保存。素材切换需要重启游戏。",L"Saved. Restart the game to switch the audio set.");
            else if(reset)set_status(L"已保存。约 1 秒生效，随机机制计数将清零。",L"Saved. Applies in about one second; rule counters will reset.");
            else if(soft_reset)set_status(L"已保存。约 1 秒生效，仅软保底计数清零。",L"Saved. Applies in about one second; only the soft-pity count resets.");
            else set_status(L"已保存。多数设置约 1 秒在游戏中生效。",L"Saved. Most settings apply in-game in about one second.");
            return true;
        }catch(const Error& e){error(e);return false;}
    }
    bool discard() {
        return !dirty || MessageBoxW(window,tr(L"放弃尚未保存的修改？",L"Discard unsaved changes?"),tr(L"尚未保存",L"Unsaved changes"),MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES;
    }
    void draw_preview(DRAWITEMSTRUCT* d) {
        RECT rect=d->rcItem;FillRect(d->hDC,&rect,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        SetDCBrushColor(d->hDC,RGB(236,242,249));FillRect(d->hDC,&rect,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        int left=2,center=40,height=50,count=10,transparency=0,scale=100;
        integer(view[Left],0,90,left,true);integer(view[Center],0,100,center,true);integer(view[Height],10,100,height,true);
        integer(view[MaxEntries],1,50,count,true);integer(view[Transparency],0,100,transparency,true);integer(view[WaiScale],0,200,scale,true);
        const float w=static_cast<float>(rect.right),h=static_cast<float>(rect.bottom),span=h*height/100.f,pitch=span/10.f,side=pitch*.86f;
        const float x=std::clamp(w*left/100.f,0.f,std::max(0.f,w-pitch*2)),top=std::clamp(h*center/100.f-span*.5f,0.f,h-span);
        const auto tint=[&](int r,int g,int b) {
            return RGB((r*(100-transparency)+236*transparency)/100,(g*(100-transparency)+242*transparency)/100,(b*(100-transparency)+249*transparency)/100);
        };
        const int saved=SaveDC(d->hDC);
        const float bottom=top+span;
        IntersectClipRect(d->hDC,0,static_cast<int>(std::max(0.f,std::min(top,bottom-count*pitch))),rect.right,static_cast<int>(bottom));
        for(int i=0;i<count;++i) {
            const float y=bottom-(static_cast<float>(i)+.5f)*pitch-.5f*side;
            SetDCBrushColor(d->hDC,view[OverlayEnabled]==L"1"?tint(58,121,186):tint(157,167,178));
            RECT row{static_cast<LONG>(x),static_cast<LONG>(y),static_cast<LONG>(x+side),static_cast<LONG>(y+side)};
            FillRect(d->hDC,&row,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            if(i%3!=2) {
                const float badge=side*(i%3==0?scale/100.f:1.f),badgeX=x+side+pitch*.13f;
                SetDCBrushColor(d->hDC,view[OverlayEnabled]!=L"1"?tint(157,167,178):(i%3==0?tint(214,144,46):tint(151,92,174)));
                RECT mark{static_cast<LONG>(badgeX),static_cast<LONG>(y),static_cast<LONG>(badgeX+badge),static_cast<LONG>(y+badge)};
                FillRect(d->hDC,&mark,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            }
        }
        RestoreDC(d->hDC,saved);
        SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,RGB(82,102,126));SelectObject(d->hDC,font);
        RECT caption=rect;caption.left+=px(70);caption.right-=px(10);
        DrawTextW(d->hDC,tr(L"游戏画面",L"Game view"),-1,&caption,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    LRESULT message(UINT msg,WPARAM wp,LPARAM lp) {
        switch(msg) {
        case DM_GETDEFID:return MAKELONG(SaveId,DC_HASDEFID);
        case WM_CREATE:dpi=GetDpiForWindow(window);build();return 0;
        case WM_COMMAND: {
            if(building)return 0;int id=LOWORD(wp),event=HIWORD(wp);
            if(id==LanguageId && event==CBN_SELCHANGE){view=collect();english=SendMessageW(GetDlgItem(window,LanguageId),CB_GETCURSEL,0,0)==1;build();return 0;}
            if(id==SaveId || id==IDOK){save_settings();return 0;}
            if(id==DefaultsId){view=defaults();build();return 0;}
            if(id==ReloadId){if(discard())try{document=editor::read(path,fallback);view=document.values;build();}catch(const Error& e){error(e);}return 0;}
            if(id>=100 && id<100+FieldCount && (event==BN_CLICKED || event==EN_CHANGE || event==CBN_SELCHANGE))update();
            return 0;
        }
        case WM_NOTIFY:if(reinterpret_cast<NMHDR*>(lp)->idFrom==TabId && reinterpret_cast<NMHDR*>(lp)->code==TCN_SELCHANGE){page=TabCtrl_GetCurSel(tabs);show_page();}return 0;
        case WM_DRAWITEM:if(wp==40){draw_preview(reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;}break;
        case WM_CTLCOLORSTATIC: {HDC dc=reinterpret_cast<HDC>(wp);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(44,54,67));return reinterpret_cast<LRESULT>(white);}
        case WM_ERASEBKGND:{RECT r;GetClientRect(window,&r);FillRect(reinterpret_cast<HDC>(wp),&r,white);return 1;}
        case WM_DPICHANGED:{view=collect();dpi=HIWORD(wp);const RECT& r=*reinterpret_cast<RECT*>(lp);SetWindowPos(window,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);build();return 0;}
        case WM_CLOSE:
            if(dirty){const int answer=MessageBoxW(window,tr(L"保存修改后关闭？",L"Save changes before closing?"),tr(L"尚未保存",L"Unsaved changes"),MB_YESNOCANCEL|MB_ICONQUESTION);if(answer==IDCANCEL || (answer==IDYES && !save_settings()))return 0;}
            DestroyWindow(window);return 0;
        case WM_DESTROY:PostQuitMessage(0);return 0;
        }
        return DefWindowProcW(window,msg,wp,lp);
    }
};
LRESULT CALLBACK proc(HWND w,UINT m,WPARAM a,LPARAM b) {
    App* app=reinterpret_cast<App*>(GetWindowLongPtrW(w,GWLP_USERDATA));
    if(m==WM_NCCREATE){app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(b)->lpCreateParams);app->window=w;SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
    if(!app)return DefWindowProcW(w,m,a,b);
    try{return app->message(m,a,b);}catch(const Error& e){app->error(e);return 0;}catch(...){MessageBoxW(w,L"Unable to complete this operation.",L"Settings",MB_OK|MB_ICONERROR);return 0;}
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
    App app;
    try {
        app.english=PRIMARYLANGID(GetUserDefaultUILanguage())!=LANG_CHINESE;
        wchar_t exe[32768]{};if(!GetModuleFileNameW(nullptr,exe,32768))throw Error(Failure::IO,GetLastError());
        app.path=std::filesystem::path(exe).parent_path()/L"voice_randomizer.ini";
        HRSRC resource=FindResourceW(instance,MAKEINTRESOURCEW(101),RT_RCDATA);
        if(!resource)throw Error(Failure::IO,GetLastError());
        const auto* data=static_cast<const unsigned char*>(LockResource(LoadResource(instance,resource)));
        app.fallback=decode(std::vector<unsigned char>(data,data+SizeofResource(instance,resource)));
        app.document=editor::read(app.path,app.fallback);app.view=app.document.values;
        INITCOMMONCONTROLSEX common{sizeof(common),ICC_TAB_CLASSES|ICC_UPDOWN_CLASS};InitCommonControlsEx(&common);
        WNDCLASSEXW wc{sizeof(wc)};wc.lpfnWndProc=proc;wc.hInstance=instance;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);wc.hbrBackground=app.white;wc.lpszClassName=L"KloseVoiceSettings";RegisterClassExW(&wc);
        app.dpi=GetDpiForSystem();RECT size{0,0,app.px(720),app.px(643)};
        const DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
        AdjustWindowRectExForDpi(&size,style,FALSE,WS_EX_CONTROLPARENT,app.dpi);
        HWND window=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"Klose Voice Mod Settings",style,CW_USEDEFAULT,CW_USEDEFAULT,size.right-size.left,size.bottom-size.top,nullptr,nullptr,instance,&app);
        if(!window)throw Error(Failure::IO,GetLastError());ShowWindow(window,show);UpdateWindow(window);
        MSG message{};
        while(GetMessageW(&message,nullptr,0,0)>0){
            if(message.message==WM_KEYDOWN && message.wParam=='S' && (GetKeyState(VK_CONTROL)&0x8000)){app.save_settings();continue;}
            if(message.message==WM_KEYDOWN && message.wParam==VK_ESCAPE){SendMessageW(window,WM_CLOSE,0,0);continue;}
            if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
        }
        return 0;
    }catch(const Error& e){app.error(e);return 1;}catch(...){MessageBoxW(nullptr,L"Cannot open the settings editor.",L"Settings",MB_OK|MB_ICONERROR);return 1;}
}
