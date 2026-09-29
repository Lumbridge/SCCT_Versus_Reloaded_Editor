#include "pch.h"
#undef min
#undef max
#include "EmitterLibraryWindow.h"
#include "EmitterLibraryModel.h"
#include "EmitterPreview.h"
#include "WorkflowEditor.h"
#include "WorkflowTools.h"
#include <commctrl.h>
#include <windowsx.h>
#include <uxtheme.h>
#include <algorithm>
#include <ctime>
#include <set>
#pragma comment(lib,"uxtheme.lib")

namespace EmitterLibraryWindow
{
using namespace Workflow;
namespace
{
constexpr COLORREF Background=RGB(240,240,240),Panel=RGB(255,255,255),Ink=RGB(0,0,0),Muted=RGB(96,96,96),Stage=RGB(26,26,26);
enum Id { Search=100,Tree,Preview,Hint,Name,Category,Description,Details,Place,SaveSelection,Edit,Delete,Restore,Status,MenuPlace=300,MenuEdit,MenuDelete };
struct State
{
    HWND window{},tree{},preview{};HFONT font{},heading{};HBRUSH background{},panel{},stage{};
    Json entries=Json::array();std::string selected,message="Select an effect to preview it.";
    unsigned revision=~0u;UINT dpi=96;int left=300,right=330,split=0;bool filling=false,attached=false;
};
HWND window=nullptr;
int Px(const State& s,int n){return MulDiv(n,s.dpi,96);}
// Library text is UTF-8; these ANSI controls use the active code page.
std::string Recode(const std::string& text,UINT from,UINT to)
{
    if(text.empty())return {};
    int count=MultiByteToWideChar(from,0,text.data(),static_cast<int>(text.size()),nullptr,0);
    std::wstring wide(count,L'\0');MultiByteToWideChar(from,0,text.data(),static_cast<int>(text.size()),wide.data(),count);
    count=WideCharToMultiByte(to,0,wide.data(),static_cast<int>(wide.size()),nullptr,0,nullptr,nullptr);
    std::string out(count,'\0');WideCharToMultiByte(to,0,wide.data(),static_cast<int>(wide.size()),out.data(),count,nullptr,nullptr);return out;
}
std::string Ansi(const std::string& utf8){return Recode(utf8,CP_UTF8,CP_ACP);}
std::string Lower(std::string text){std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return text;}
std::string Text(HWND w){int n=GetWindowTextLengthA(w);std::string text(n+1,'\0');GetWindowTextA(w,text.data(),n+1);text.resize(n);return text;}
HWND Item(State& s,int id){return GetDlgItem(s.window,id);}
void Set(State& s,int id,const std::string& text){SetWindowTextA(Item(s,id),text.c_str());}
void StatusText(State& s,const std::string& text){Set(s,Status,text);}
HWND Add(State& s,const char* type,const char* text,int id,DWORD style=0,DWORD ex=0)
{
    auto w=CreateWindowExA(ex,type,text,WS_CHILD|WS_VISIBLE|style,0,0,10,10,s.window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandle(nullptr),nullptr);
    SendMessage(w,WM_SETFONT,reinterpret_cast<WPARAM>(s.font),TRUE);SetWindowTheme(w,L"Explorer",nullptr);return w;
}
void Button(State& s,const char* text,int id){Add(s,"BUTTON",text,id,BS_PUSHBUTTON|WS_TABSTOP);}
void Bounds(State& s,int id,int x,int y,int w,int h){MoveWindow(Item(s,id),Px(s,x),Px(s,y),Px(s,std::max(1,w)),Px(s,std::max(1,h)),TRUE);}
const Json* Current(const State& s)
{
    for(const auto& entry:s.entries)if(entry.at("id").get<std::string>()==s.selected)return &entry;
    return nullptr;
}
void Layout(State& s)
{
    RECT r{};GetClientRect(s.window,&r);int w=MulDiv(r.right,96,s.dpi),h=MulDiv(r.bottom,96,s.dpi);
    s.left=std::clamp(s.left,220,std::max(220,w-640));s.right=std::clamp(s.right,260,std::max(260,w-s.left-360));
    const int x=s.left+6,c=w-s.left-s.right-12,rx=w-s.right+6,rw=s.right-18;
    Bounds(s,Search,12,12,s.left-18,26);Bounds(s,Tree,12,46,s.left-18,h-92);
    Bounds(s,Preview,x,12,c,h-78);Bounds(s,Hint,x,h-60,c,20);
    Bounds(s,Name,rx,10,rw,30);Bounds(s,Category,rx,42,rw,20);Bounds(s,Description,rx,68,rw,108);Bounds(s,Details,rx,184,rw,std::max(60,h-184-212));
    Bounds(s,Place,rx,h-218,rw,34);Bounds(s,SaveSelection,rx,h-176,rw,28);Bounds(s,Edit,rx,h-142,rw,28);
    Bounds(s,Delete,rx,h-108,(rw-6)/2,28);Bounds(s,Restore,rx+(rw-6)/2+6,h-108,rw-(rw-6)/2-6,28);
    Bounds(s,Status,12,h-36,w-24,28);
    EmitterPreview::Resize();
}
std::string Date(const Json& entry)
{
    long long ms=0;try{ms=std::stoll(entry.value("modified",std::string("0")));}catch(...){}
    if(ms<=0)return {};
    std::time_t t=static_cast<std::time_t>(ms/1000);std::tm local{};if(localtime_s(&local,&t))return {};
    char text[64]{};std::strftime(text,sizeof(text),"%d %b %Y %H:%M",&local);return text;
}
// A short, factual summary of what placing the entry adds to the map.
std::string DetailText(const Json& entry)
{
    size_t actors=entry.at("actors").size(),systems=0;bool triggered=false;std::set<std::string> classes;
    for(const auto& actor:entry.at("actors"))
    {
        const auto text=actor.value("text",std::string{});
        for(size_t at=text.find("Begin Object");at!=std::string::npos;at=text.find("Begin Object",at+12))++systems;
        if(text.find("Disabled=True")!=std::string::npos)triggered=true;
        classes.insert(actor.value("class",std::string("Emitter")));
    }
    std::string out=entry.value("builtin",false)?"Built-in effect":"Your effect";
    if(!entry.value("builtin",false)){auto date=Date(entry);if(!date.empty())out+=", saved "+date;}
    out+="\r\n"+std::to_string(actors)+(actors==1?" emitter actor, ":" emitter actors, ")+std::to_string(systems)+(systems==1?" particle system":" particle systems");
    if(triggered)out+="\r\nTriggered: it plays when an event matching its Tag fires. The preview loops it.";
    std::set<std::string> packages;
    for(const auto& dependency:entry.value("dependencies",Json::array()))
    {
        auto path=dependency.get<std::string>();auto dot=path.find('.');
        if(dot!=std::string::npos && path.rfind("Engine.",0)!=0 && path.rfind("Core.",0)!=0)packages.insert(path.substr(0,dot));
    }
    if(!packages.empty()){out+="\r\n\r\nPackages loaded when placed:";for(const auto& p:packages)out+="\r\n  "+p;}
    if(entry.contains("source") && !entry.at("source").get<std::string>().empty())out+="\r\n\r\nFrom "+entry.at("source").get<std::string>();
    return Ansi(out);
}
void Paint(State& s,const std::string& message){s.message=message;InvalidateRect(s.preview,nullptr,TRUE);}
void ShowEntry(State& s)
{
    const auto* entry=Current(s);
    EnableWindow(Item(s,Place),entry!=nullptr);EnableWindow(Item(s,Edit),entry!=nullptr);EnableWindow(Item(s,Delete),entry!=nullptr);
    if(!entry)
    {
        Set(s,Name,"");Set(s,Category,"");Set(s,Description,"");Set(s,Details,"");
        EmitterPreview::Clear();Paint(s,"Select an effect to preview it.");return;
    }
    Set(s,Name,Ansi(entry->at("name").get<std::string>()));Set(s,Category,Ansi(entry->at("category").get<std::string>()));
    auto description=Ansi(entry->value("description",std::string{}));
    for(size_t at=description.find('\n');at!=std::string::npos;at=description.find('\n',at+2))if(!at || description[at-1]!='\r')description.insert(at,"\r");
    Set(s,Description,description.empty()?"No description.":description);Set(s,Details,DetailText(*entry));
    SetWindowTextA(Item(s,Edit),entry->value("builtin",false)?"Save a copy to edit...":"Edit name, category, description...");
    SetWindowTextA(Item(s,Delete),entry->value("builtin",false)?"Hide":"Delete...");
    // Showing may load texture packages, which can take a moment.
    StatusText(s,"Loading the preview of "+Ansi(entry->at("name").get<std::string>())+"...");UpdateWindow(Item(s,Status));
    try
    {
        std::string error;
        // One viewport per panel: closing hides the window, so the viewport is only
        // created again after the editor destroys the window on exit.
        if(!s.attached){if(!EmitterPreview::Attach(s.preview,error))throw std::runtime_error(error.empty()?"The preview is not available.":error);s.attached=true;}
        EmitterPreview::Show(*entry);
        StatusText(s,"Previewing "+Ansi(entry->at("name").get<std::string>())+". Place puts it at the builder brush; double-click an effect does the same.");
    }
    catch(const std::exception& e){EmitterPreview::Clear();Paint(s,std::string("No preview: ")+e.what());StatusText(s,std::string("No preview: ")+e.what());}
}
bool Matches(const Json& entry,const std::string& filter)
{
    if(filter.empty())return true;
    for(const char* key:{"name","category","description","source"})if(Lower(Ansi(entry.value(key,std::string{}))).find(filter)!=std::string::npos)return true;
    return false;
}
void Fill(State& s)
{
    s.entries=Editor::EmitterLibrary();s.revision=Editor::EmitterLibraryRevision();
    const auto filter=Lower(Text(Item(s,Search)));
    std::vector<std::string> order;for(const auto& c:Editor::EmitterCategories())order.push_back(c.get<std::string>());
    for(const auto& entry:s.entries)if(std::find(order.begin(),order.end(),entry.at("category").get<std::string>())==order.end())order.push_back(entry.at("category"));
    s.filling=true;SendMessage(s.tree,WM_SETREDRAW,FALSE,0);TreeView_DeleteAllItems(s.tree);
    HTREEITEM select=nullptr,first=nullptr;size_t shown=0;
    for(const auto& category:order)
    {
        std::vector<size_t> rows;for(size_t i=0;i<s.entries.size();++i)if(s.entries[i].at("category").get<std::string>()==category && Matches(s.entries[i],filter))rows.push_back(i);
        if(rows.empty())continue;
        auto label=Ansi(category)+" ("+std::to_string(rows.size())+")";
        TVINSERTSTRUCTA parent{};parent.hInsertAfter=TVI_LAST;parent.item.mask=TVIF_TEXT|TVIF_PARAM|TVIF_STATE;parent.item.pszText=label.data();parent.item.lParam=-1;parent.item.state=parent.item.stateMask=TVIS_BOLD;
        auto group=reinterpret_cast<HTREEITEM>(SendMessageA(s.tree,TVM_INSERTITEMA,0,reinterpret_cast<LPARAM>(&parent)));
        for(auto i:rows)
        {
            auto name=Ansi(s.entries[i].at("name").get<std::string>());if(!s.entries[i].value("builtin",false))name+="  (yours)";
            TVINSERTSTRUCTA child{};child.hParent=group;child.hInsertAfter=TVI_LAST;child.item.mask=TVIF_TEXT|TVIF_PARAM;child.item.pszText=name.data();child.item.lParam=static_cast<LPARAM>(i);
            auto item=reinterpret_cast<HTREEITEM>(SendMessageA(s.tree,TVM_INSERTITEMA,0,reinterpret_cast<LPARAM>(&child)));
            if(!first)first=item;if(s.entries[i].at("id").get<std::string>()==s.selected)select=item;++shown;
        }
        TreeView_Expand(s.tree,group,TVE_EXPAND);
    }
    SendMessage(s.tree,WM_SETREDRAW,TRUE,0);s.filling=false;
    const auto previous=s.selected;
    if(!select)select=first;
    if(select){TreeView_SelectItem(s.tree,select);TreeView_EnsureVisible(s.tree,select);TVITEMA item{};item.mask=TVIF_PARAM;item.hItem=select;TreeView_GetItem(s.tree,&item);s.selected=s.entries[item.lParam].at("id");}
    else s.selected.clear();
    // A hidden window leaves the preview empty; Open shows the selection again.
    if((s.selected!=previous || !EmitterPreview::Active()) && IsWindowVisible(s.window))ShowEntry(s);
    if(!shown)StatusText(s,filter.empty()?"The library is empty. Restore built-ins, or select emitters in the map and save them.":"No effect matches the search.");
    auto problems=Editor::EmitterLibraryProblems();
    if(!problems.empty())StatusText(s,std::to_string(problems.size())+" saved entr"+(problems.size()==1?"y":"ies")+" in emitter_library.json could not be read and "+(problems.size()==1?"is":"are")+" not listed.");
}
void Select(State& s,const std::string& id){s.selected=id;Fill(s);}
void PlaceCurrent(State& s)
{
    const auto* entry=Current(s);if(!entry)throw std::runtime_error("Select an effect to place.");
    auto members=Editor::PlaceEmitterEntry(*entry,Editor::BuilderPose());
    StatusText(s,"Placed "+Ansi(entry->at("name").get<std::string>())+" at the builder brush ("+std::to_string(members.size())+(members.size()==1?" actor, selected). Undo removes it.":" actors, selected). Undo removes them."));
}
void EditCurrent(State& s)
{
    const auto* found=Current(s);if(!found)throw std::runtime_error("Select an effect first.");
    const auto entry=*found;
    if(entry.value("builtin",false))
    {
        auto changes=WorkflowTools::AskEmitterDetails(s.window,entry,"Save a copy of this built-in effect");if(changes.is_null())return;
        auto copy=entry;for(auto& [key,value]:changes.items())copy[key]=value;
        auto saved=Editor::SaveEmitterEntry(copy);Select(s,saved.at("id"));StatusText(s,"Saved "+Ansi(saved.at("name").get<std::string>())+" as your own effect.");
        return;
    }
    auto changes=WorkflowTools::AskEmitterDetails(s.window,entry,"Edit effect");if(changes.is_null())return;
    auto updated=Editor::UpdateEmitterEntry(entry.at("id"),changes);Select(s,updated.at("id"));StatusText(s,"Updated "+Ansi(updated.at("name").get<std::string>())+".");
}
void DeleteCurrent(State& s)
{
    const auto* found=Current(s);if(!found)throw std::runtime_error("Select an effect first.");
    const auto entry=*found;const auto name=Ansi(entry.at("name").get<std::string>());
    const bool builtin=entry.value("builtin",false);
    auto question=builtin?"Hide the built-in effect '"+name+"'? Restore Built-ins brings it back.":"Delete '"+name+"' from your Emitter Library? This cannot be undone.";
    if(MessageBoxA(s.window,question.c_str(),"Emitter Library",MB_OKCANCEL|MB_ICONQUESTION)!=IDOK)return;
    // Keep the neighbour selected so the list does not jump to the top.
    std::string next;
    if(auto item=TreeView_GetSelection(s.tree))for(auto other:{TreeView_GetNextSibling(s.tree,item),TreeView_GetPrevSibling(s.tree,item)})
        if(other && next.empty()){TVITEMA row{};row.mask=TVIF_PARAM;row.hItem=other;TreeView_GetItem(s.tree,&row);if(row.lParam>=0)next=s.entries[row.lParam].at("id");}
    Editor::DeleteEmitterEntry(entry.at("id"));Select(s,next);StatusText(s,(builtin?"Hid ":"Deleted ")+name+".");
}
void Command(State& s,int id)
{
    if(id==Place)PlaceCurrent(s);
    else if(id==SaveSelection)
    {
        auto saved=WorkflowTools::SaveEmitterSelection(s.window,false);
        if(!saved.is_null()){Select(s,saved.at("id"));StatusText(s,"Saved "+Ansi(saved.at("name").get<std::string>())+" to the library.");}
    }
    else if(id==Edit)EditCurrent(s);
    else if(id==Delete)DeleteCurrent(s);
    else if(id==Restore){Editor::RestoreBuiltinEmitters();Fill(s);StatusText(s,"Hidden built-in effects are listed again.");}
}
LRESULT CALLBACK PreviewProc(HWND w,UINT message,WPARAM wp,LPARAM lp)
{
    if(message==WM_ERASEBKGND){RECT r{};GetClientRect(w,&r);auto brush=CreateSolidBrush(Stage);FillRect(reinterpret_cast<HDC>(wp),&r,brush);DeleteObject(brush);return 1;}
    if(message==WM_PAINT)
    {
        PAINTSTRUCT paint{};auto dc=BeginPaint(w,&paint);
        if(window)if(auto s=reinterpret_cast<State*>(GetWindowLongPtr(window,GWLP_USERDATA)))
        {
            RECT r{};GetClientRect(w,&r);InflateRect(&r,-Px(*s,24),0);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(170,170,170));
            auto old=SelectObject(dc,s->font);DrawTextA(dc,s->message.c_str(),-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);SelectObject(dc,old);
        }
        EndPaint(w,&paint);return 0;
    }
    return DefWindowProcA(w,message,wp,lp);
}
LRESULT CALLBACK SearchProc(HWND w,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR)
{
    auto result=DefSubclassProc(w,message,wp,lp);
    if(message==WM_PAINT && GetWindowTextLengthA(w)==0 && GetFocus()!=w)
    {
        auto dc=GetDC(w);RECT r{};GetClientRect(w,&r);r.left+=6;SetBkMode(dc,TRANSPARENT);SetTextColor(dc,Muted);auto font=SelectObject(dc,reinterpret_cast<HFONT>(SendMessage(w,WM_GETFONT,0,0)));DrawTextA(dc,"Search effects",-1,&r,DT_SINGLELINE|DT_VCENTER);SelectObject(dc,font);ReleaseDC(w,dc);
    }
    return result;
}
void Fonts(State& s)
{
    if(s.font)DeleteObject(s.font);if(s.heading)DeleteObject(s.heading);
    s.font=CreateFontA(-Px(s,14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,"Segoe UI");
    s.heading=CreateFontA(-Px(s,20),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,"Segoe UI");
}
LRESULT CALLBACK WindowProc(HWND w,UINT message,WPARAM wp,LPARAM lp)
{
    auto s=reinterpret_cast<State*>(GetWindowLongPtr(w,GWLP_USERDATA));if(message==WM_NCCREATE){s=static_cast<State*>(reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams);s->window=w;SetWindowLongPtr(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));}if(!s)return DefWindowProcA(w,message,wp,lp);
    try
    {
        if(message==WM_CREATE)
        {
            s->dpi=GetDpiForWindow(w);Fonts(*s);s->background=CreateSolidBrush(Background);s->panel=CreateSolidBrush(Panel);
            Add(*s,"EDIT","",Search,ES_AUTOHSCROLL|WS_TABSTOP,WS_EX_CLIENTEDGE);SetWindowSubclass(Item(*s,Search),SearchProc,1,0);
            s->tree=Add(*s,WC_TREEVIEWA,"",Tree,TVS_HASBUTTONS|TVS_LINESATROOT|TVS_SHOWSELALWAYS|TVS_FULLROWSELECT|WS_TABSTOP|WS_BORDER);TreeView_SetItemHeight(s->tree,Px(*s,24));
            WNDCLASSA wc{};wc.lpfnWndProc=PreviewProc;wc.hInstance=GetModuleHandle(nullptr);wc.lpszClassName="ReloadedEmitterLibraryPreview";wc.hCursor=LoadCursor(nullptr,IDC_SIZEALL);RegisterClassA(&wc);
            s->preview=CreateWindowExA(0,wc.lpszClassName,"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,10,10,w,reinterpret_cast<HMENU>(Preview),wc.hInstance,nullptr);
            Add(*s,"STATIC","Drag to orbit, right-drag or wheel to zoom, double-click to reset the view.",Hint,SS_CENTER);
            Add(*s,"STATIC","",Name,SS_ENDELLIPSIS|SS_NOPREFIX);SendMessage(Item(*s,Name),WM_SETFONT,reinterpret_cast<WPARAM>(s->heading),TRUE);
            Add(*s,"STATIC","",Category,SS_ENDELLIPSIS|SS_NOPREFIX);
            Add(*s,"EDIT","",Description,ES_MULTILINE|ES_READONLY|WS_VSCROLL|ES_AUTOVSCROLL);Add(*s,"EDIT","",Details,ES_MULTILINE|ES_READONLY|WS_VSCROLL|ES_AUTOVSCROLL);
            Button(*s,"Place at builder brush",Place);Button(*s,"Save selected emitters as new effect...",SaveSelection);Button(*s,"Edit name, category, description...",Edit);Button(*s,"Delete...",Delete);Button(*s,"Restore built-ins",Restore);
            Add(*s,"STATIC","",Status,SS_ENDELLIPSIS|SS_NOPREFIX);
            Layout(*s);Fill(*s);SetTimer(w,1,500,nullptr);SetFocus(s->tree);return 0;
        }
        if(message==WM_SIZE){Layout(*s);return 0;}
        if(message==WM_GETMINMAXINFO){auto m=reinterpret_cast<MINMAXINFO*>(lp);m->ptMinTrackSize={Px(*s,900),Px(*s,560)};return 0;}
        if(message==WM_DPICHANGED)
        {
            s->dpi=HIWORD(wp);auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(w,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER);
            Fonts(*s);for(HWND child=GetWindow(w,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))SendMessage(child,WM_SETFONT,reinterpret_cast<WPARAM>(child==Item(*s,Name)?s->heading:s->font),TRUE);
            TreeView_SetItemHeight(s->tree,Px(*s,24));Layout(*s);return 0;
        }
        if(message==WM_LBUTTONDOWN){RECT r{};GetClientRect(w,&r);int x=GET_X_LPARAM(lp);if(std::abs(x-Px(*s,s->left))<Px(*s,8))s->split=1;else if(std::abs(x-(r.right-Px(*s,s->right)))<Px(*s,8))s->split=2;if(s->split)SetCapture(w);return 0;}
        if(message==WM_MOUSEMOVE && s->split){RECT r{};GetClientRect(w,&r);int x=MulDiv(GET_X_LPARAM(lp),96,s->dpi);if(s->split==1)s->left=x;else s->right=MulDiv(r.right,96,s->dpi)-x;Layout(*s);return 0;}
        if(message==WM_LBUTTONUP || message==WM_CAPTURECHANGED){s->split=0;if(GetCapture()==w)ReleaseCapture();return 0;}
        if(message==WM_SETCURSOR && reinterpret_cast<HWND>(wp)==w){POINT p{};GetCursorPos(&p);ScreenToClient(w,&p);RECT r{};GetClientRect(w,&r);if(std::abs(p.x-Px(*s,s->left))<Px(*s,8) || std::abs(p.x-(r.right-Px(*s,s->right)))<Px(*s,8)){SetCursor(LoadCursor(nullptr,IDC_SIZEWE));return TRUE;}}
        if(message==WM_COMMAND)
        {
            const int id=LOWORD(wp),notification=HIWORD(wp);
            if(id==Search && notification==EN_CHANGE){Fill(*s);return 0;}
            if(id==MenuPlace || id==MenuEdit || id==MenuDelete){Command(*s,id==MenuPlace?Place:id==MenuEdit?Edit:Delete);return 0;}
            if(notification==BN_CLICKED && id>=Place && id<=Restore){Command(*s,id);return 0;}
        }
        if(message==WM_NOTIFY)
        {
            auto header=reinterpret_cast<NMHDR*>(lp);
            if(header->idFrom==Tree && header->code==TVN_SELCHANGEDA && !s->filling)
            {
                auto change=reinterpret_cast<NMTREEVIEWA*>(lp);
                if(change->itemNew.lParam>=0 && change->itemNew.lParam<static_cast<LPARAM>(s->entries.size())){s->selected=s->entries[change->itemNew.lParam].at("id");ShowEntry(*s);}
                return 0;
            }
            if(header->idFrom==Tree && (header->code==NM_DBLCLK || header->code==NM_RCLICK))
            {
                POINT p{};GetCursorPos(&p);TVHITTESTINFO hit{};hit.pt=p;ScreenToClient(s->tree,&hit.pt);
                auto item=TreeView_HitTest(s->tree,&hit);if(!item)return 0;
                TVITEMA row{};row.mask=TVIF_PARAM;row.hItem=item;TreeView_GetItem(s->tree,&row);if(row.lParam<0)return 0;
                TreeView_SelectItem(s->tree,item);
                if(header->code==NM_DBLCLK){Command(*s,Place);return 1;}
                const bool builtin=s->entries[row.lParam].value("builtin",false);
                auto menu=CreatePopupMenu();AppendMenuA(menu,MF_STRING,MenuPlace,"&Place at builder brush");AppendMenuA(menu,MF_SEPARATOR,0,nullptr);
                AppendMenuA(menu,MF_STRING,MenuEdit,builtin?"Save a &copy to edit...":"&Edit name, category, description...");AppendMenuA(menu,MF_STRING,MenuDelete,builtin?"&Hide":"&Delete...");
                auto command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,w,nullptr);DestroyMenu(menu);
                if(command)SendMessage(w,WM_COMMAND,command,0);return 1;
            }
            if(header->idFrom==Tree && header->code==TVN_KEYDOWN)
            {
                auto key=reinterpret_cast<NMTVKEYDOWN*>(lp)->wVKey;
                if(key==VK_RETURN){Command(*s,Place);return 1;}
                if(key==VK_DELETE){Command(*s,Delete);return 1;}
                if(key==VK_F5){Fill(*s);return 1;}
            }
            return 0;
        }
        if(message==WM_TIMER){if(IsWindowVisible(w) && s->revision!=Editor::EmitterLibraryRevision())Fill(*s);return 0;}
        if(message==WM_CTLCOLORSTATIC || message==WM_CTLCOLOREDIT)
        {
            const bool readOnly=message==WM_CTLCOLORSTATIC && (reinterpret_cast<HWND>(lp)==Item(*s,Description) || reinterpret_cast<HWND>(lp)==Item(*s,Details));
            SetTextColor(reinterpret_cast<HDC>(wp),reinterpret_cast<HWND>(lp)==Item(*s,Category) || reinterpret_cast<HWND>(lp)==Item(*s,Hint)?Muted:Ink);
            SetBkColor(reinterpret_cast<HDC>(wp),message==WM_CTLCOLOREDIT || readOnly?Panel:Background);
            return reinterpret_cast<LRESULT>(message==WM_CTLCOLOREDIT || readOnly?s->panel:s->background);
        }
        if(message==WM_ERASEBKGND){RECT r{};GetClientRect(w,&r);FillRect(reinterpret_cast<HDC>(wp),&r,s->background);return 1;}
        // Closing hides the window and empties the preview; the editor destroys it on exit.
        if(message==WM_CLOSE){try{WriteDocument(Editor::Directory()/"emitter-library-window.json",{{"version",1},{"left",s->left},{"right",s->right}});}catch(...){}EmitterPreview::Clear();ShowWindow(w,SW_HIDE);return 0;}
        // Children are destroyed after this: delete the preview viewport first.
        if(message==WM_DESTROY){KillTimer(w,1);EmitterPreview::Detach();return 0;}
        if(message==WM_NCDESTROY){window=nullptr;DeleteObject(s->font);DeleteObject(s->heading);DeleteObject(s->background);DeleteObject(s->panel);SetWindowLongPtr(w,GWLP_USERDATA,0);delete s;return DefWindowProcA(w,message,wp,lp);}
    }
    catch(const std::exception& e)
    {
        if(message==WM_TIMER || message==WM_CREATE)StatusText(*s,e.what());
        else MessageBoxA(w,e.what(),"Emitter Library",MB_OK|MB_ICONERROR);
        return message==WM_CREATE?0:1;
    }
    return DefWindowProcA(w,message,wp,lp);
}
}
void Open(HWND owner)
{
    if(window)
    {
        ShowWindow(window,IsIconic(window)?SW_RESTORE:SW_SHOW);SetForegroundWindow(window);
        if(auto s=reinterpret_cast<State*>(GetWindowLongPtr(window,GWLP_USERDATA)))Fill(*s);
        return;
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_TREEVIEW_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    WNDCLASSA wc{};wc.lpfnWndProc=WindowProc;wc.hInstance=GetModuleHandle(nullptr);wc.lpszClassName="ReloadedEmitterLibrary";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassA(&wc);
    auto s=new State;
    try{auto prefs=ReadDocument(Editor::Directory()/"emitter-library-window.json",Json::object());s->left=std::clamp(prefs.value("left",300),220,700);s->right=std::clamp(prefs.value("right",330),260,700);}catch(...){}
    window=CreateWindowExA(WS_EX_CONTROLPARENT,wc.lpszClassName,"Emitter Library",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1240,760,owner,nullptr,wc.hInstance,s);
    if(!window){delete s;throw std::runtime_error("The Emitter Library window could not be created.");}
    ShowWindow(window,SW_SHOW);
    if(!EmitterPreview::Active())ShowEntry(*s);
}
}
