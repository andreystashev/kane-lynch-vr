#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <new>
#include <string>

// Presentation only: all controls retain their original Win32 styles, names,
// focus handling and native automatic check/radio behaviour.
namespace VrModLauncherTheme {
inline constexpr COLORREF Background=RGB(17,19,23);
inline constexpr COLORREF Surface=RGB(25,28,33);
inline constexpr COLORREF Input=RGB(35,39,46);
inline constexpr COLORREF Text=RGB(239,241,244);
inline constexpr COLORREF Muted=RGB(156,163,175);
inline constexpr COLORREF Accent=RGB(194,40,49);
inline constexpr COLORREF Border=RGB(58,63,73);
inline constexpr COLORREF Good=RGB(109,202,157);
inline constexpr COLORREF Warning=RGB(231,182,105);

inline void Fill(HDC dc,const RECT& rectangle,COLORREF color) {
    const auto brush=CreateSolidBrush(color);
    FillRect(dc,&rectangle,brush);DeleteObject(brush);
}
inline void Rounded(HDC dc,const RECT& rectangle,int radius,COLORREF color,COLORREF edge) {
    const auto brush=CreateSolidBrush(color);
    const auto pen=CreatePen(PS_SOLID,1,edge);
    const auto previousBrush=SelectObject(dc,brush),previousPen=SelectObject(dc,pen);
    RoundRect(dc,rectangle.left,rectangle.top,rectangle.right,rectangle.bottom,radius*2,radius*2);
    SelectObject(dc,previousPen);SelectObject(dc,previousBrush);DeleteObject(pen);DeleteObject(brush);
}
inline void DrawCard(HDC dc,const RECT& rectangle,int radius=12) {
    Rounded(dc,rectangle,radius,Surface,Surface);
}
inline void DrawBackground(HDC dc,const RECT& rectangle) {
    Fill(dc,rectangle,Background);
}
inline void StyleWindow(HWND window) {
    // Load dynamically so older Windows versions need no additional import.
    if(const auto library=LoadLibraryExW(L"dwmapi.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32)){
        using SetAttribute=HRESULT(WINAPI*)(HWND,DWORD,LPCVOID,DWORD);
        if(const auto set=reinterpret_cast<SetAttribute>(GetProcAddress(library,"DwmSetWindowAttribute"))){
            const BOOL dark=TRUE;
            if(FAILED(set(window,20,&dark,sizeof(dark))))set(window,19,&dark,sizeof(dark));
        }
        FreeLibrary(library);
    }
}

namespace Detail {
inline BOOL QuerySubclass(HWND window,SUBCLASSPROC procedure,UINT_PTR id,DWORD_PTR* data){
    // comctl32 v5 exports this helper by ordinal on older x86 Windows.
    const auto module=GetModuleHandleW(L"comctl32.dll");
    using Query=BOOL(WINAPI*)(HWND,SUBCLASSPROC,UINT_PTR,DWORD_PTR*);
    auto query=module?reinterpret_cast<Query>(GetProcAddress(module,"GetWindowSubclass")):nullptr;
    if(!query&&module)query=reinterpret_cast<Query>(GetProcAddress(module,MAKEINTRESOURCEA(411)));
    return query?query(window,procedure,id,data):FALSE;
}
struct ButtonState {bool hot=false;int scale=100;COLORREF background=Surface;};
inline bool IsCheck(LONG_PTR style) {
    const auto type=style&BS_TYPEMASK;
    return type==BS_CHECKBOX||type==BS_AUTOCHECKBOX||type==BS_3STATE||type==BS_AUTO3STATE;
}
inline bool IsSegment(HWND window) {
    const int id=GetDlgCtrlID(window);
    return id>=200&&id<300;
}
inline void Focus(HDC dc,RECT rectangle,COLORREF color) {
    InflateRect(&rectangle,-3,-3);
    const auto pen=CreatePen(PS_DOT,1,color);
    const auto previousPen=SelectObject(dc,pen);
    const auto previousBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));
    const auto previousBk=SetBkMode(dc,TRANSPARENT);
    Rectangle(dc,rectangle.left,rectangle.top,rectangle.right,rectangle.bottom);
    SetBkMode(dc,previousBk);SelectObject(dc,previousBrush);SelectObject(dc,previousPen);DeleteObject(pen);
}
inline void PaintButton(HWND window,HDC dc,const ButtonState& appearance) {
    RECT bounds{};GetClientRect(window,&bounds);
    const int saved=SaveDC(dc);
    Fill(dc,bounds,appearance.background);
    const auto font=reinterpret_cast<HFONT>(SendMessageW(window,WM_GETFONT,0,0));
    if(font)SelectObject(dc,font);
    SetBkMode(dc,TRANSPARENT);
    const auto unit=[&](int value){return std::max(1,MulDiv(value,appearance.scale,100));};
    const auto style=GetWindowLongPtrW(window,GWL_STYLE);
    const auto nativeState=SendMessageW(window,BM_GETSTATE,0,0);
    const auto check=SendMessageW(window,BM_GETCHECK,0,0);
    const bool enabled=IsWindowEnabled(window)!=FALSE,pressed=(nativeState&BST_PUSHED)!=0;
    const bool checked=check==BST_CHECKED,checkbox=IsCheck(style),segment=IsSegment(window);
    const bool primary=GetDlgCtrlID(window)==320;
    const bool focused=(nativeState&BST_FOCUS)!=0&&!(SendMessageW(window,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS);
    const COLORREF foreground=enabled?Text:Muted;
    SetTextColor(dc,foreground);
    RECT text=bounds;
    if(checkbox){
        const int side=std::max(10,std::min(unit(19),static_cast<int>(bounds.bottom-bounds.top)-unit(4)));
        RECT box{unit(1),(bounds.bottom-side)/2,unit(1)+side,(bounds.bottom+side)/2};
        const bool active=checked||check==BST_INDETERMINATE;
        const COLORREF fill=active?(enabled?Accent:Border):Input;
        Rounded(dc,box,unit(4),fill,enabled&&appearance.hot?Muted:active?fill:Border);
        if(active){
            const auto pen=CreatePen(PS_SOLID,std::max(1,unit(2)),enabled?Text:Muted);
            const auto previous=SelectObject(dc,pen);
            if(check==BST_INDETERMINATE){
                MoveToEx(dc,box.left+side/4,box.top+side/2,nullptr);LineTo(dc,box.right-side/4,box.top+side/2);
            }else{
                POINT points[]={{box.left+side/4,box.top+side/2},{box.left+side*9/20,box.top+side*7/10},{box.left+side*4/5,box.top+side*3/10}};
                Polyline(dc,points,3);
            }
            SelectObject(dc,previous);DeleteObject(pen);
        }
        text.left=unit(27);text.right-=unit(4);
        const UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX;
        const int length=GetWindowTextLengthW(window);
        std::wstring caption(static_cast<size_t>(length)+1,L'\0');
        caption.resize(GetWindowTextW(window,caption.data(),static_cast<int>(caption.size())));
        DrawTextW(dc,caption.c_str(),static_cast<int>(caption.size()),&text,flags);
    }else{
        RECT face=bounds;InflateRect(&face,-1,-1);
        COLORREF fill=Input,edge=Border;
        if(segment){
            fill=checked?RGB(53,32,37):Surface;edge=checked?RGB(91,43,48):Surface;
            if(appearance.hot&&!checked)fill=Input;
        }else if(primary){
            fill=enabled?(pressed?RGB(145,26,34):appearance.hot?RGB(217,49,59):Accent):Border;
            edge=fill;
        }else if(enabled){
            fill=pressed?RGB(29,32,38):appearance.hot?RGB(47,52,61):Input;
            edge=appearance.hot?RGB(82,88,101):Border;
        }
        Rounded(dc,face,unit(6),fill,edge);
        if(segment&&checked){
            RECT indicator{face.left+unit(16),face.bottom-unit(3),face.right-unit(16),face.bottom-unit(1)};
            Fill(dc,indicator,Accent);
        }
        text.left+=unit(10);text.right-=unit(10);
        const UINT flags=DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX;
        const int length=GetWindowTextLengthW(window);
        std::wstring caption(static_cast<size_t>(length)+1,L'\0');
        caption.resize(GetWindowTextW(window,caption.data(),static_cast<int>(caption.size())));
        DrawTextW(dc,caption.c_str(),static_cast<int>(caption.size()),&text,flags);
    }
    if(focused)Focus(dc,bounds,primary?Text:Muted);
    RestoreDC(dc,saved);
}
inline LRESULT CALLBACK ButtonProcedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam,
                                       UINT_PTR id,DWORD_PTR reference) {
    auto* state=reinterpret_cast<ButtonState*>(reference);
    if(message==WM_NCDESTROY){
        RemoveWindowSubclass(window,ButtonProcedure,id);
        const auto result=DefSubclassProc(window,message,wparam,lparam);delete state;return result;
    }
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_PRINTCLIENT){PaintButton(window,reinterpret_cast<HDC>(wparam),*state);return 0;}
    if(message==WM_PAINT){
        PAINTSTRUCT paint{};const auto dc=BeginPaint(window,&paint);
        RECT bounds{};GetClientRect(window,&bounds);
        const auto buffer=CreateCompatibleDC(dc);
        const auto bitmap=CreateCompatibleBitmap(dc,std::max<LONG>(1,bounds.right),std::max<LONG>(1,bounds.bottom));
        if(buffer&&bitmap){
            const auto previous=SelectObject(buffer,bitmap);PaintButton(window,buffer,*state);
            BitBlt(dc,0,0,bounds.right,bounds.bottom,buffer,0,0,SRCCOPY);SelectObject(buffer,previous);
        }else PaintButton(window,dc,*state);
        if(bitmap)DeleteObject(bitmap);
        if(buffer)DeleteDC(buffer);
        EndPaint(window,&paint);return 0;
    }
    if(message==WM_MOUSEMOVE&&!state->hot){
        state->hot=true;TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,window,0};TrackMouseEvent(&tracking);
        InvalidateRect(window,nullptr,FALSE);
    }
    if(message==WM_MOUSELEAVE){state->hot=false;InvalidateRect(window,nullptr,FALSE);}
    // Automatic check/radio transitions, dialog navigation and keyboard input all
    // remain the responsibility of the original native BUTTON procedure.
    const auto result=DefSubclassProc(window,message,wparam,lparam);
    switch(message){
    case WM_KEYDOWN:case WM_KEYUP:case WM_LBUTTONDOWN:case WM_LBUTTONUP:case WM_CAPTURECHANGED:
    case WM_ENABLE:case WM_SETTEXT:case WM_SETFONT:case WM_SETFOCUS:case WM_KILLFOCUS:
    case BM_SETCHECK:case BM_SETSTATE:case BM_SETSTYLE:case WM_UPDATEUISTATE:
        InvalidateRect(window,nullptr,FALSE);break;
    }
    return result;
}
struct ComboState {int scale=100;bool hot=false;};
inline void PaintComboFrame(HWND window,HDC dc,const ComboState& appearance) {
    COMBOBOXINFO info{};info.cbSize=sizeof(info);
    RECT bounds{};GetClientRect(window,&bounds);
    if(!GetComboBoxInfo(window,&info)||bounds.right<4||bounds.bottom<4)return;
    const int saved=SaveDC(dc);
    const bool enabled=IsWindowEnabled(window)!=FALSE;
    const bool dropped=SendMessageW(window,CB_GETDROPPEDSTATE,0,0)!=0;
    const COLORREF edge=enabled&&(appearance.hot||GetFocus()==window)?RGB(94,101,115):Border;
    // The combo still owns layout, hit testing and its popup. Cover only its
    // standard two-pixel frame and arrow area after native painting completes.
    constexpr int inset=2;
    Fill(dc,RECT{0,0,bounds.right,inset},Input);
    Fill(dc,RECT{0,bounds.bottom-inset,bounds.right,bounds.bottom},Input);
    Fill(dc,RECT{0,0,inset,bounds.bottom},Input);
    Fill(dc,RECT{bounds.right-inset,0,bounds.right,bounds.bottom},Input);
    RECT arrowArea=info.rcButton;
    arrowArea.left=std::max<LONG>(inset,arrowArea.left-3);
    arrowArea.top=inset;arrowArea.right=bounds.right-inset;arrowArea.bottom=bounds.bottom-inset;
    Fill(dc,arrowArea,enabled&&(appearance.hot||dropped)?RGB(47,52,61):Input);
    const auto pen=CreatePen(PS_SOLID,1,edge);
    const auto previousPen=SelectObject(dc,pen),previousBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));
    Rectangle(dc,0,0,bounds.right,bounds.bottom);
    SelectObject(dc,previousBrush);SelectObject(dc,previousPen);DeleteObject(pen);
    const int centerX=(info.rcButton.left+info.rcButton.right)/2,centerY=(info.rcButton.top+info.rcButton.bottom)/2;
    const int radius=std::max(2,MulDiv(4,appearance.scale,100));
    const auto arrowPen=CreatePen(PS_SOLID,1,enabled?Text:Muted);
    const auto previousArrow=SelectObject(dc,arrowPen);
    POINT arrow[]={{centerX-radius,centerY-radius/2},{centerX,centerY+radius/2},{centerX+radius,centerY-radius/2}};
    Polyline(dc,arrow,3);SelectObject(dc,previousArrow);DeleteObject(arrowPen);
    RestoreDC(dc,saved);
}
inline LRESULT CALLBACK ComboProcedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam,
                                      UINT_PTR id,DWORD_PTR reference) {
    auto* state=reinterpret_cast<ComboState*>(reference);
    if(message==WM_NCDESTROY){
        RemoveWindowSubclass(window,ComboProcedure,id);
        const auto result=DefSubclassProc(window,message,wparam,lparam);delete state;return result;
    }
    if(message==WM_MOUSEMOVE&&!state->hot){
        state->hot=true;TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,window,0};TrackMouseEvent(&tracking);
        InvalidateRect(window,nullptr,FALSE);
    }
    if(message==WM_MOUSELEAVE){state->hot=false;InvalidateRect(window,nullptr,FALSE);}
    const auto result=DefSubclassProc(window,message,wparam,lparam);
    if(message==WM_PAINT){
        if(const auto dc=GetDC(window)){PaintComboFrame(window,dc,*state);ReleaseDC(window,dc);}
    }else if(message==WM_PRINTCLIENT&&wparam)PaintComboFrame(window,reinterpret_cast<HDC>(wparam),*state);
    else if(message==WM_ENABLE||message==WM_SETFOCUS||message==WM_KILLFOCUS||message==WM_LBUTTONUP)
        InvalidateRect(window,nullptr,FALSE);
    return result;
}
}
inline void StyleButton(HWND window,int scale=100,COLORREF background=Surface) {
    if(!window)return;
    DWORD_PTR existing=0;
    if(Detail::QuerySubclass(window,Detail::ButtonProcedure,1,&existing)){
        auto* state=reinterpret_cast<Detail::ButtonState*>(existing);
        state->scale=scale;state->background=background;InvalidateRect(window,nullptr,FALSE);return;
    }
    auto* state=new(std::nothrow) Detail::ButtonState;
    if(!state)return;
    state->scale=scale;state->background=background;
    if(!SetWindowSubclass(window,Detail::ButtonProcedure,1,reinterpret_cast<DWORD_PTR>(state)))delete state;
}
inline void StyleCombo(HWND window,int scale=100) {
    if(!window)return;
    DWORD_PTR existing=0;
    if(Detail::QuerySubclass(window,Detail::ComboProcedure,1,&existing)){
        reinterpret_cast<Detail::ComboState*>(existing)->scale=scale;InvalidateRect(window,nullptr,FALSE);return;
    }
    auto* state=new(std::nothrow) Detail::ComboState;
    if(!state)return;
    state->scale=scale;
    if(!SetWindowSubclass(window,Detail::ComboProcedure,1,reinterpret_cast<DWORD_PTR>(state)))delete state;
}
inline bool HandleDrawItem(const DRAWITEMSTRUCT* item,HFONT font) {
    if(!item||item->CtlType!=ODT_COMBOBOX)return false;
    const int saved=SaveDC(item->hDC);
    const bool selected=(item->itemState&ODS_SELECTED)!=0,disabled=(item->itemState&ODS_DISABLED)!=0;
    Fill(item->hDC,item->rcItem,selected?RGB(65,36,42):Input);
    if(font)SelectObject(item->hDC,font);
    SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,disabled?Muted:Text);
    if(item->itemID!=static_cast<UINT>(-1)){
        const auto length=SendMessageW(item->hwndItem,CB_GETLBTEXTLEN,item->itemID,0);
        if(length!=CB_ERR){
            std::wstring label(static_cast<size_t>(length)+1,L'\0');
            const auto copied=SendMessageW(item->hwndItem,CB_GETLBTEXT,item->itemID,reinterpret_cast<LPARAM>(label.data()));
            if(copied!=CB_ERR){
                label.resize(static_cast<size_t>(copied));RECT text=item->rcItem;text.left+=10;text.right-=8;
                DrawTextW(item->hDC,label.c_str(),static_cast<int>(label.size()),&text,DT_SINGLELINE|DT_VCENTER|DT_LEFT|DT_NOPREFIX);
            }
        }
    }
    if((item->itemState&ODS_FOCUS)&&!(item->itemState&ODS_NOFOCUSRECT))Detail::Focus(item->hDC,item->rcItem,Muted);
    RestoreDC(item->hDC,saved);return true;
}
}
