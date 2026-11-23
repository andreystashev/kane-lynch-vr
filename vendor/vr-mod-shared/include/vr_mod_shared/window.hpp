#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <memory>
#include <exception>
#include <cwchar>
#include <cstdio>
#include "model.hpp"
#include "theme.hpp"

// Data-driven Windows shell. Adapters own INI semantics and game processes;
// this module owns controls, pages, browsing, scrolling and worker lifetimes.
namespace VrModLauncher {
namespace WindowDetail {
namespace Theme=VrModLauncherTheme;
constexpr UINT Progress=WM_APP+30,Finished=WM_APP+31,SupportFinished=WM_APP+32;
constexpr int Play=320,Save=321,Check=322,Cancel=323,Support=324,FirstAction=400,FirstField=1000;
struct Placement {HWND handle{};int y{},height{};size_t page{};};
struct Widget {std::wstring id;Kind kind{};HWND input{},browse{},discover{};int command{};};
struct Context {
    App app;HWND window{},panel{},status{};HFONT font{},titleFont{};HBRUSH background{},input{};
    std::vector<Widget> widgets;std::vector<Placement> placements;std::vector<int> heights;
    std::vector<HWND> tabs;size_t page=0;int scale=100,scroll=0,viewport=390;bool populating=false,busy=false,smoke=false;
    unsigned smokeChecks=0,smokeStage=0;HANDLE worker{},supportWorker{};
};
struct Job {
    HWND window{};Model model;App app;bool launch=false,support=false;std::wstring destination;
};
struct Result {bool okay=false,launch=false;std::wstring text;};
inline int U(const Context& c,int value){return MulDiv(value,c.scale,100);}
inline std::wstring Text(HWND handle){std::wstring text(static_cast<size_t>(GetWindowTextLengthW(handle))+1,L'\0');text.resize(GetWindowTextW(handle,text.data(),static_cast<int>(text.size())));return text;}
inline void Status(Context& c,const std::wstring& text){SetWindowTextW(c.status,text.c_str());SendMessageW(c.status,EM_SETSEL,0,0);}
inline HWND Control(Context& c,HWND parent,const wchar_t* type,const wchar_t* label,DWORD style,int id,int x,int y,int width,int height){
    auto handle=CreateWindowExW(0,type,label,WS_CHILD|WS_VISIBLE|style,U(c,x),U(c,y),U(c,width),U(c,height),parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(handle,WM_SETFONT,reinterpret_cast<WPARAM>(c.font),FALSE);
    if(!_wcsicmp(type,L"BUTTON"))Theme::StyleButton(handle,c.scale,Theme::Background);
    return handle;
}
inline int TextHeight(Context& c,const std::wstring& text,int width){
    if(text.empty())return 0;HDC dc=GetDC(c.window);auto previous=SelectObject(dc,c.font);RECT bounds{0,0,U(c,width),0};
    DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&bounds,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);SelectObject(dc,previous);ReleaseDC(c.window,dc);
    return std::max(23,MulDiv(bounds.bottom,100,c.scale)+5);
}
inline void Place(Context& c,HWND handle,size_t page,int y,int height){c.placements.push_back({handle,y,height,page});}
inline void Layout(Context& c){
    const int height=c.heights.empty()?0:c.heights[c.page];const int maxScroll=std::max(0,height-c.viewport);c.scroll=std::clamp(c.scroll,0,maxScroll);
    SCROLLINFO info{};info.cbSize=sizeof(info);info.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;info.nMin=0;info.nMax=std::max(0,height-1);info.nPage=c.viewport;info.nPos=c.scroll;SetScrollInfo(c.panel,SB_VERT,&info,TRUE);
    for(const auto& item:c.placements){ShowWindow(item.handle,item.page==c.page?SW_SHOW:SW_HIDE);if(item.page==c.page){RECT r{};GetWindowRect(item.handle,&r);POINT p{r.left,r.top};ScreenToClient(c.panel,&p);SetWindowPos(item.handle,nullptr,p.x,U(c,item.y-c.scroll),0,0,SWP_NOZORDER|SWP_NOSIZE|SWP_NOACTIVATE);}}
    for(size_t i=0;i<c.tabs.size();++i)SendMessageW(c.tabs[i],BM_SETCHECK,i==c.page?BST_CHECKED:BST_UNCHECKED,0);
    InvalidateRect(c.panel,nullptr,TRUE);
}
inline void SelectPage(Context& c,size_t page){if(page>=c.app.model.pages.size())return;c.page=page;c.scroll=0;Layout(c);}
inline LRESULT CALLBACK TabProcedure(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR reference){
    auto* c=reinterpret_cast<Context*>(reference);
    if(message==WM_NCDESTROY){RemoveWindowSubclass(window,TabProcedure,id);return DefSubclassProc(window,message,wp,lp);}
    // Windows dialog radio-group traversal must not enter the nested page
    // panel. Keep arrow navigation explicitly within the finite tab strip.
    if(message==WM_GETDLGCODE)return DefSubclassProc(window,message,wp,lp)|DLGC_WANTARROWS;
    if((message==WM_KEYDOWN||message==WM_KEYUP)&&(wp==VK_LEFT||wp==VK_RIGHT||wp==VK_UP||wp==VK_DOWN)){
        if(message==WM_KEYDOWN&&!c->tabs.empty()){
            const auto index=static_cast<size_t>(GetDlgCtrlID(window)-200);
            const auto step=(wp==VK_LEFT||wp==VK_UP)?c->tabs.size()-1:1;
            const auto next=(index+step)%c->tabs.size();SelectPage(*c,next);SetFocus(c->tabs[next]);
        }return 0;
    }
    return DefSubclassProc(window,message,wp,lp);
}
inline void Gather(Context& c){
    for(const auto& w:c.widgets){auto& f=c.app.model.Get(w.id);if(f.kind==Kind::Note||f.kind==Kind::Hidden)continue;
        if(f.kind==Kind::Boolean)f.value=SendMessageW(w.input,BM_GETCHECK,0,0)==BST_CHECKED?L"1":L"0";
        else if(f.kind==Kind::Choice){const auto index=SendMessageW(w.input,CB_GETCURSEL,0,0);if(index>=0&&static_cast<size_t>(index)<f.choices.size())f.value=f.choices[static_cast<size_t>(index)];}
        else f.value=Text(w.input);
    }
}
inline void Populate(Context& c){
    c.populating=true;
    for(const auto& w:c.widgets){const auto& f=c.app.model.Get(w.id);
        if(f.kind==Kind::Note){
            const auto value=f.label+(f.value.empty()?L"":L"\r\n"+f.value)+(f.help.empty()?L"":L"\r\n"+f.help);
            if(Text(w.input)!=value){SetWindowTextW(w.input,value.c_str());const int height=TextHeight(c,value,770);
                auto at=std::find_if(c.placements.begin(),c.placements.end(),[&](const Placement& p){return p.handle==w.input;});
                if(at!=c.placements.end()){const int delta=height-at->height;at->height=height;SetWindowPos(w.input,nullptr,0,0,U(c,770),U(c,height),SWP_NOMOVE|SWP_NOZORDER);for(auto next=at+1;next!=c.placements.end();++next)if(next->page==at->page)next->y+=delta;c.heights[at->page]+=delta;}}
            continue;
        }
        if(f.kind==Kind::Hidden)continue;
        if(f.kind==Kind::Boolean)SendMessageW(w.input,BM_SETCHECK,f.value==L"1"?BST_CHECKED:BST_UNCHECKED,0);
        else if(f.kind==Kind::Choice){const auto index=std::find(f.choices.begin(),f.choices.end(),f.value)-f.choices.begin();SendMessageW(w.input,CB_SETCURSEL,static_cast<size_t>(index)<f.choices.size()?index:-1,0);}
        else if(Text(w.input)!=f.value)SetWindowTextW(w.input,f.value.c_str());
        EnableWindow(w.input,!c.busy&&f.enabled);if(w.browse)EnableWindow(w.browse,!c.busy&&f.enabled);if(w.discover)EnableWindow(w.discover,!c.busy&&f.enabled);
    }
    for(int id:{Play,Save,Check})EnableWindow(GetDlgItem(c.window,id),!c.busy);
    EnableWindow(GetDlgItem(c.window,Play),!c.busy&&static_cast<bool>(c.app.launch));EnableWindow(GetDlgItem(c.window,Check),!c.busy&&static_cast<bool>(c.app.inspect));
    EnableWindow(GetDlgItem(c.window,Support),!c.supportWorker);c.populating=false;if(!c.heights.empty())Layout(c);
}
inline void Changed(Context& c){try{Gather(c);if(c.app.changed)c.app.changed(c.app.model);Populate(c);}catch(const std::exception&){Status(c,L"Could not refresh settings. The game adapter returned an invalid field.");}}
inline bool Validate(Context& c,std::wstring& error){Gather(c);if(!ValidateModel(c.app.model,error))return false;return !c.app.validate||c.app.validate(c.app.model,error);}
inline bool SaveSettings(Context& c){
    std::wstring error;bool okay=false;
    try{okay=Validate(c,error)&&c.app.save&&c.app.save(c.app.model,error);}catch(const std::exception&){error=L"The game adapter could not save these settings.";}
    if(!okay){MessageBoxW(c.window,error.empty()?L"Settings could not be saved.":error.c_str(),c.app.title.c_str(),MB_OK|MB_ICONERROR);return false;}
    Populate(c);Status(c,L"Settings saved. Changes apply the next time you start the game.");return true;
}
inline DWORD WINAPI Worker(void* opaque){
    std::unique_ptr<Job> job(static_cast<Job*>(opaque));auto result=std::make_unique<Result>();result->launch=job->launch;
    try{
        if(job->support)result->okay=job->app.support(job->model,job->destination,result->text);
        else if(job->launch)result->okay=job->app.launch(job->model,result->text,[window=job->window](const std::wstring& text){auto p=std::make_unique<std::wstring>(text);if(PostMessageW(window,Progress,0,reinterpret_cast<LPARAM>(p.get())))p.release();});
        else result->okay=job->app.inspect(job->model,result->text);
    }catch(const std::exception&){result->text=L"The requested operation failed in the game adapter.";}catch(...){result->text=L"The requested operation failed.";}
    if(PostMessageW(job->window,job->support?SupportFinished:Finished,0,reinterpret_cast<LPARAM>(result.get())))result.release();return 0;
}
inline void Begin(Context& c,bool launch){
    if(c.busy)return;std::wstring error;
    if(launch){if(!SaveSettings(c))return;}else if(!Validate(c,error)){MessageBoxW(c.window,error.c_str(),c.app.title.c_str(),MB_OK|MB_ICONERROR);return;}
    auto job=std::make_unique<Job>();job->window=c.window;job->model=c.app.model;job->app=c.app;job->launch=launch;
    c.worker=CreateThread(nullptr,0,Worker,job.get(),0,nullptr);if(!c.worker){Status(c,L"Could not start the check or launch.");return;}job.release();c.busy=true;Populate(c);
    Status(c,launch?L"Checking the connection and starting the game...":L"Checking the game and mod setup...");if(launch&&!c.smoke)ShowWindow(c.window,SW_MINIMIZE);
}
inline void BeginSupport(Context& c){
    if(c.supportWorker||!c.app.support)return;
    auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);BROWSEINFOW browse{};browse.hwndOwner=c.window;browse.lpszTitle=L"Choose a folder for the local support report";browse.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    auto selected=SHBrowseForFolderW(&browse);wchar_t folder[MAX_PATH]{};const bool okay=selected&&SHGetPathFromIDListW(selected,folder);if(selected)CoTaskMemFree(selected);if(SUCCEEDED(initialized))CoUninitialize();if(!okay)return;
    Gather(c);auto job=std::make_unique<Job>();job->window=c.window;job->app=c.app;job->model=c.app.model;job->support=true;job->destination=folder;
    c.supportWorker=CreateThread(nullptr,0,Worker,job.get(),0,nullptr);if(!c.supportWorker){Status(c,L"Could not start the support report.");return;}job.release();Populate(c);Status(c,L"Creating a local support report...");
}
inline void Browse(Context& c,const Widget& widget){
    wchar_t path[32768]{};const auto current=Text(widget.input);std::wcsncpy(path,current.c_str(),std::size(path)-1);OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=c.window;
    dialog.lpstrFilter=widget.id==L"runtime"?L"OpenXR runtime JSON\0*.json\0\0":L"Applications\0*.exe\0All files\0*.*\0\0";dialog.lpstrFile=path;dialog.nMaxFile=static_cast<DWORD>(std::size(path));dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(GetOpenFileNameW(&dialog)){SetWindowTextW(widget.input,path);Changed(c);}
}
inline void Create(Context& c){
    auto title=Control(c,c.window,L"STATIC",c.app.heading.c_str(),SS_LEFT,0,24,22,790,42);SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(c.titleFont),0);
    Control(c,c.window,L"STATIC",c.app.description.c_str(),SS_LEFT,0,24,68,790,48);
    const int tabsWidth=790/static_cast<int>(c.app.model.pages.size());
    // Layout already owns exclusive tab selection. Automatic radio processing
    // can loop through a WS_EX_CONTROLPARENT panel when a native click runs.
    for(size_t i=0;i<c.app.model.pages.size();++i){
        auto tab=Control(c,c.window,L"BUTTON",c.app.model.pages[i].label.c_str(),WS_TABSTOP|BS_RADIOBUTTON|BS_PUSHLIKE|(i==0?WS_GROUP:0),200+static_cast<int>(i),24+static_cast<int>(i)*tabsWidth,127,tabsWidth-8,34);
        c.tabs.push_back(tab);SetWindowSubclass(tab,TabProcedure,2,reinterpret_cast<DWORD_PTR>(&c));
    }
    c.panel=CreateWindowExW(WS_EX_CONTROLPARENT,L"VrModLauncherPanel-v1",L"",WS_CHILD|WS_VISIBLE|WS_GROUP|WS_VSCROLL|WS_CLIPCHILDREN,U(c,12),U(c,180),U(c,816),U(c,c.viewport),c.window,nullptr,GetModuleHandleW(nullptr),&c);
    int command=FirstField;
    for(size_t page=0;page<c.app.model.pages.size();++page){int y=8;
        for(const auto& f:c.app.model.pages[page].fields){
            if(f.kind==Kind::Hidden)continue;
            Widget w;w.id=f.id;w.kind=f.kind;w.command=command;command+=3;
            auto label=[&](const std::wstring& value,int height){auto h=Control(c,c.panel,L"STATIC",value.c_str(),SS_LEFT,0,12,y,770,height);Place(c,h,page,y,height);y+=height;};
            if(f.kind==Kind::Note){const auto value=f.label+(f.value.empty()?L"":L"\r\n"+f.value)+(f.help.empty()?L"":L"\r\n"+f.help);const int height=TextHeight(c,value,770);w.input=Control(c,c.panel,L"STATIC",value.c_str(),SS_LEFT,0,12,y,770,height);Place(c,w.input,page,y,height);y+=height;}
            else {
                if(f.kind!=Kind::Boolean){label(f.label,TextHeight(c,f.label,770));y+=4;}
                if(f.kind==Kind::Boolean)w.input=Control(c,c.panel,L"BUTTON",f.label.c_str(),WS_TABSTOP|BS_AUTOCHECKBOX|BS_NOTIFY,w.command,12,y,770,28);
                else if(f.kind==Kind::Choice){w.input=Control(c,c.panel,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,w.command,12,y,770,220);for(const auto& choice:f.choices)SendMessageW(w.input,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(choice.c_str()));}
                else {const bool file=f.kind==Kind::File;const bool discover=file&&f.id==L"game"&&c.app.discover;
                    w.input=Control(c,c.panel,L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,w.command,12,y,file?(discover?510:640):770,30);
                    if(file){w.browse=Control(c,c.panel,L"BUTTON",L"Browse...",WS_TABSTOP|BS_PUSHBUTTON,w.command+1,discover?532:662,y,120,30);Place(c,w.browse,page,y,30);}
                    if(discover){w.discover=Control(c,c.panel,L"BUTTON",L"Find game",WS_TABSTOP|BS_PUSHBUTTON,w.command+2,662,y,120,30);Place(c,w.discover,page,y,30);}
                }
                Place(c,w.input,page,y,30);y+=36;
            }
            if(f.kind!=Kind::Note&&!f.help.empty())label(f.help,TextHeight(c,f.help,770));y+=22;c.widgets.push_back(w);
        }c.heights.push_back(y);
    }
    c.status=Control(c,c.window,L"EDIT",L"Check is optional. Save & play starts the mod.",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,0,24,592,790,66);
    Control(c,c.window,L"BUTTON",L"Save & play",WS_TABSTOP|BS_PUSHBUTTON|WS_GROUP,Play,24,682,240,34);
    Control(c,c.window,L"BUTTON",L"Save",WS_TABSTOP|BS_DEFPUSHBUTTON,Save,276,682,144,34);
    Control(c,c.window,L"BUTTON",L"Check setup",WS_TABSTOP|BS_PUSHBUTTON,Check,432,682,240,34);
    Control(c,c.window,L"BUTTON",L"Close",WS_TABSTOP|BS_PUSHBUTTON,Cancel,684,682,132,34);
    const auto actionCount=c.app.actions.size()+(c.app.support?1u:0u);int actionX=24;
    const int actionWidth=actionCount?790/static_cast<int>(actionCount):790;
    if(c.app.support){Control(c,c.window,L"BUTTON",c.app.supportLabel.empty()?L"Support report...":c.app.supportLabel.c_str(),WS_TABSTOP|BS_PUSHBUTTON,Support,actionX,545,actionWidth-8,32);actionX+=actionWidth;}
    for(size_t i=0;i<c.app.actions.size();++i){Control(c,c.window,L"BUTTON",c.app.actions[i].label.c_str(),WS_TABSTOP|BS_PUSHBUTTON,FirstAction+static_cast<int>(i),actionX,545,actionWidth-8,32);actionX+=actionWidth;}
    // Support button shares the footer area, reducing the scrollable page only.
    if(actionCount){c.viewport=350;SetWindowPos(c.panel,nullptr,0,0,U(c,816),U(c,c.viewport),SWP_NOMOVE|SWP_NOZORDER);}
    Populate(c);SelectPage(c,0);
}
inline LRESULT CALLBACK PanelProc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto* c=reinterpret_cast<Context*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){c=static_cast<Context*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(c));}
    if(!c)return DefWindowProcW(window,message,wp,lp);
    if(message==WM_ERASEBKGND){RECT r{};GetClientRect(window,&r);Theme::Fill(reinterpret_cast<HDC>(wp),r,Theme::Background);return 1;}
    if(message==WM_COMMAND||message==WM_CTLCOLORSTATIC||message==WM_CTLCOLOREDIT||message==WM_CTLCOLORLISTBOX||message==WM_DRAWITEM||message==WM_MEASUREITEM)return SendMessageW(c->window,message,wp,lp);
    if(message==WM_MOUSEWHEEL){c->scroll-=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*54;Layout(*c);return 0;}
    if(message==WM_VSCROLL){SCROLLINFO info{};info.cbSize=sizeof(info);info.fMask=SIF_ALL;GetScrollInfo(window,SB_VERT,&info);switch(LOWORD(wp)){case SB_LINEUP:c->scroll-=30;break;case SB_LINEDOWN:c->scroll+=30;break;case SB_PAGEUP:c->scroll-=c->viewport;break;case SB_PAGEDOWN:c->scroll+=c->viewport;break;case SB_THUMBTRACK:c->scroll=info.nTrackPos;break;case SB_TOP:c->scroll=0;break;case SB_BOTTOM:c->scroll=info.nMax;break;}Layout(*c);return 0;}
    return DefWindowProcW(window,message,wp,lp);
}
inline LRESULT ProcedureImpl(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto* c=reinterpret_cast<Context*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){c=static_cast<Context*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);c->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(c));}
    if(!c)return DefWindowProcW(window,message,wp,lp);
    if(message==WM_CREATE){Create(*c);if(c->smoke)SetTimer(window,7,50,nullptr);return 0;}
    if(message==WM_PAINT){PAINTSTRUCT p{};auto dc=BeginPaint(window,&p);RECT r{};GetClientRect(window,&r);Theme::DrawBackground(dc,r);EndPaint(window,&p);return 0;}
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_CTLCOLORSTATIC||message==WM_CTLCOLOREDIT||message==WM_CTLCOLORLISTBOX){auto dc=reinterpret_cast<HDC>(wp);SetTextColor(dc,Theme::Text);const bool field=message!=WM_CTLCOLORSTATIC||reinterpret_cast<HWND>(lp)==c->status;SetBkColor(dc,field?Theme::Input:Theme::Background);return reinterpret_cast<LRESULT>(field?c->input:c->background);}
    if(message==WM_DRAWITEM&&Theme::HandleDrawItem(reinterpret_cast<DRAWITEMSTRUCT*>(lp),c->font))return TRUE;
    if(message==WM_COMMAND){const int id=LOWORD(wp),notification=HIWORD(wp);
        if(notification==EN_SETFOCUS||notification==CBN_SETFOCUS||notification==BN_SETFOCUS){
            const auto focus=reinterpret_cast<HWND>(lp);auto at=std::find_if(c->placements.begin(),c->placements.end(),[&](const Placement& p){return p.handle==focus;});
            if(at!=c->placements.end()){if(at->page!=c->page)SelectPage(*c,at->page);if(at->y<c->scroll)c->scroll=at->y;else if(at->y+at->height>c->scroll+c->viewport)c->scroll=at->y+at->height-c->viewport;Layout(*c);}}
        if(id>=200&&id<200+static_cast<int>(c->tabs.size())){SelectPage(*c,id-200);return 0;}
        if(id>=FirstAction&&id<FirstAction+static_cast<int>(c->app.actions.size())){Gather(*c);if(c->app.actions[id-FirstAction].run)c->app.actions[id-FirstAction].run(c->app.model);return 0;}
        if(id==Cancel){SendMessageW(window,WM_CLOSE,0,0);return 0;}if(id==Support){BeginSupport(*c);return 0;}if(c->busy||c->populating)return 0;
        if(id==Save){SaveSettings(*c);return 0;}if(id==Play||id==Check){Begin(*c,id==Play);return 0;}
        for(const auto& w:c->widgets){if(id==w.command+1&&w.browse){Browse(*c,w);return 0;}if(id==w.command+2&&w.discover){auto found=c->app.discover();if(found.empty())Status(*c,L"Game not found in installed Steam libraries. Select its executable manually.");else {SetWindowTextW(w.input,found.c_str());Changed(*c);Status(*c,L"Installed game found. Save to keep this location.");}return 0;}
            if(id==w.command&&(notification==BN_CLICKED||notification==CBN_SELCHANGE||notification==EN_CHANGE)){KillTimer(window,8);SetTimer(window,8,notification==EN_CHANGE?300:1,nullptr);return 0;}}
    }
    if(message==WM_TIMER&&wp==8){KillTimer(window,8);if(!c->busy)Changed(*c);return 0;}
    if(message==Progress){std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lp));Status(*c,*text);return 0;}
    if(message==Finished||message==SupportFinished){std::unique_ptr<Result> result(reinterpret_cast<Result*>(lp));auto& worker=message==Finished?c->worker:c->supportWorker;if(worker){WaitForSingleObject(worker,INFINITE);CloseHandle(worker);worker=nullptr;}if(message==Finished)c->busy=false;Populate(*c);Status(*c,result->text);
        // A successful adapter can return as soon as VR is ready, while the
        // game is still running. Keep the settings window minimized then.
        if(result->launch&&!result->okay){ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);}if(!result->okay||message==SupportFinished)MessageBoxW(window,result->text.c_str(),c->app.title.c_str(),MB_OK|(result->okay?MB_ICONINFORMATION:MB_ICONERROR));return 0;}
    if(message==WM_TIMER&&wp==7&&c->smoke){KillTimer(window,7);
        // Exercise native button processing, not just our SelectPage helper.
        if(!c->smokeStage&&c->tabs.size()>1){c->smokeStage=1;PostMessageW(c->tabs.back(),WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(10,10));PostMessageW(c->tabs.back(),WM_LBUTTONUP,0,MAKELPARAM(10,10));SetTimer(window,7,100,nullptr);return 0;}
        if(c->smokeStage==1&&c->page==c->tabs.size()-1){c->smokeStage=2;PostMessageW(c->tabs.back(),WM_KEYDOWN,VK_LEFT,0);PostMessageW(c->tabs.back(),WM_KEYUP,VK_LEFT,0);SetTimer(window,7,100,nullptr);return 0;}
        bool okay=IsWindow(GetDlgItem(window,Play))&&c->widgets.size()>0&&(!c->smokeStage||(c->smokeStage==2&&c->page==c->tabs.size()-2));Model before=c->app.model;
        for(size_t page=0;page<c->tabs.size();++page){SelectPage(*c,page);for(const auto& p:c->placements)okay=okay&&((IsWindowVisible(p.handle)!=FALSE)==(p.page==page));}
        for(const auto& w:c->widgets)if(w.kind==Kind::Note){const auto old=c->app.model.Get(w.id).value;c->app.model.Get(w.id).value=old+L" dynamic note fixture";Populate(*c);okay=okay&&Text(w.input).find(L"dynamic note fixture")!=std::wstring::npos;c->app.model.Get(w.id).value=old;Populate(*c);}
        for(const auto& p:c->placements)if(p.y>c->viewport&&(GetWindowLongPtrW(p.handle,GWL_STYLE)&WS_TABSTOP)){SelectPage(*c,p.page);SendMessageW(window,WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(p.handle),EN_SETFOCUS),reinterpret_cast<LPARAM>(p.handle));okay=okay&&c->scroll>0;break;}
        Gather(*c);for(const auto& page:before.pages)for(const auto& f:page.fields)okay=okay&&f.value==c->app.model.Get(f.id).value;
        c->busy=true;Populate(*c);for(const auto& w:c->widgets)if(w.input&&w.kind!=Kind::Note)okay=okay&&!IsWindowEnabled(w.input);c->busy=false;Populate(*c);c->smokeChecks=okay?1:0;DestroyWindow(window);return 0;}
    if(message==WM_CLOSE){if(c->busy||c->supportWorker){ShowWindow(window,SW_MINIMIZE);return 0;}DestroyWindow(window);return 0;}
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,wp,lp);
}
inline LRESULT CALLBACK Procedure(HWND window,UINT message,WPARAM wp,LPARAM lp){
    try{return ProcedureImpl(window,message,wp,lp);}catch(const std::exception&){
        if(auto* c=reinterpret_cast<Context*>(GetWindowLongPtrW(window,GWLP_USERDATA)))Status(*c,L"The game adapter could not complete this action.");
        return message==WM_CREATE?-1:0;
    }catch(...){return message==WM_CREATE?-1:0;}
}
} // WindowDetail
inline int Run(HINSTANCE instance,App app,bool smoke=false){
    using namespace WindowDetail;std::wstring error;if(app.id.empty()||!ValidateModel(app.model,error)){if(!smoke)MessageBoxW(nullptr,error.empty()?L"Missing application ID":error.c_str(),L"Launcher configuration",MB_OK|MB_ICONERROR);return 2;}
    Context c;c.app=std::move(app);c.smoke=smoke;
    try{if(c.app.changed)c.app.changed(c.app.model);}catch(...){if(!smoke)MessageBoxW(nullptr,L"The game adapter could not load these settings.",c.app.title.c_str(),MB_OK|MB_ICONERROR);return 2;}
    if(!ValidateModel(c.app.model,error))return 2;
    const std::wstring className=L"VrModLauncher-"+c.app.id;HANDLE mutex=CreateMutexW(nullptr,FALSE,(L"Local\\"+className).c_str());
    if(!smoke&&mutex&&GetLastError()==ERROR_ALREADY_EXISTS){if(auto existing=FindWindowW(className.c_str(),nullptr)){ShowWindow(existing,SW_RESTORE);SetForegroundWindow(existing);}CloseHandle(mutex);return 0;}
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    RECT work{};if(!SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0))work={0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)};
    c.scale=std::max(55,static_cast<int>(std::min<LONG>(100,std::min((work.right-work.left-24)*100/840,(work.bottom-work.top-60)*100/744))));
    c.font=CreateFontW(-U(c,16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");c.titleFont=CreateFontW(-U(c,27),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");c.background=CreateSolidBrush(Theme::Background);c.input=CreateSolidBrush(Theme::Input);
    WNDCLASSW panel{};panel.hInstance=instance;panel.lpfnWndProc=PanelProc;panel.lpszClassName=L"VrModLauncherPanel-v1";panel.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));RegisterClassW(&panel);
    WNDCLASSW type{};type.hInstance=instance;type.lpfnWndProc=Procedure;type.lpszClassName=className.c_str();type.hCursor=panel.hCursor;RegisterClassW(&type);
    constexpr DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN;RECT bounds{0,0,U(c,840),U(c,744)};AdjustWindowRect(&bounds,style,FALSE);
    const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    HWND window=CreateWindowExW(WS_EX_CONTROLPARENT,className.c_str(),c.app.title.c_str(),style,work.left+(work.right-work.left-width)/2,work.top+(work.bottom-work.top-height)/2,width,height,nullptr,nullptr,instance,&c);
    int result=2;if(window){Theme::StyleWindow(window);ShowWindow(window,smoke?SW_SHOWNOACTIVATE:SW_SHOW);UpdateWindow(window);MSG message{};BOOL next=0;while((next=GetMessageW(&message,nullptr,0,0))>0){if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}result=next<0?2:0;}
    // Threads own snapshots and only communicate using heap-owned messages. An
    // abnormal message-loop exit still joins them before callback captures die.
    for(auto handle:{c.worker,c.supportWorker})if(handle){WaitForSingleObject(handle,INFINITE);CloseHandle(handle);}
    MSG pending{};while(PeekMessageW(&pending,nullptr,Progress,SupportFinished,PM_REMOVE)){if(pending.message==Progress)delete reinterpret_cast<std::wstring*>(pending.lParam);else delete reinterpret_cast<Result*>(pending.lParam);}
    if(window&&IsWindow(window))DestroyWindow(window);if(c.font)DeleteObject(c.font);if(c.titleFont)DeleteObject(c.titleFont);if(c.background)DeleteObject(c.background);if(c.input)DeleteObject(c.input);if(mutex)CloseHandle(mutex);UnregisterClassW(className.c_str(),instance);UnregisterClassW(panel.lpszClassName,instance);
    if(smoke){std::printf("Shared launcher UI smoke: %s; page visibility, field roundtrip, busy lock; no save/launch.\n",c.smokeChecks?"passed":"FAILED");return c.smokeChecks?0:3;}return result;
}
inline std::wstring PackageFolder(){wchar_t path[32768]{};const auto n=GetModuleFileNameW(nullptr,path,static_cast<DWORD>(std::size(path)));if(!n||n>=std::size(path))return {};std::wstring value(path,n);auto at=value.find_last_of(L"\\/");return at==std::wstring::npos?L"":value.substr(0,at);}
inline bool SmokeRequested(){int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);bool smoke=false;for(int i=1;i<count;++i)if(!std::wcscmp(args[i],L"--ui-smoke"))smoke=true;if(args)LocalFree(args);return smoke;}
} // VrModLauncher
