#include "pch.h"
#include "MapAuthoringDialog.h"
#include "WorkflowEditor.h"
#include <commdlg.h>
#include <fstream>
#include <stdexcept>

namespace MapAuthoringDialog
{
namespace
{
    struct Preview { std::wstring text;bool accepted=false; };
    std::wstring Wide(const std::string& text)
    {
        auto count=MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0);
        std::wstring result(count,L'\0');MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),result.data(),count);return result;
    }
    LRESULT CALLBACK PreviewProc(HWND window,UINT message,WPARAM w,LPARAM l)
    {
        auto state=reinterpret_cast<Preview*>(GetWindowLongPtr(window,GWLP_USERDATA));
        if(message==WM_CREATE)
        {
            state=static_cast<Preview*>(reinterpret_cast<CREATESTRUCT*>(l)->lpCreateParams);SetWindowLongPtr(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));
            auto edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",state->text.c_str(),WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_HSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,0,0,0,0,window,reinterpret_cast<HMENU>(10),nullptr,nullptr);
            SendMessage(edit,EM_SETLIMITTEXT,0,0);
            CreateWindowW(L"BUTTON",L"Apply changes",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,0,0,0,0,window,reinterpret_cast<HMENU>(IDOK),nullptr,nullptr);
            CreateWindowW(L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,0,0,window,reinterpret_cast<HMENU>(IDCANCEL),nullptr,nullptr);
            for(int id:{10,IDOK,IDCANCEL})SendMessage(GetDlgItem(window,id),WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
            return 0;
        }
        if(message==WM_SIZE)
        {
            int width=LOWORD(l),height=HIWORD(l);
            MoveWindow(GetDlgItem(window,10),12,12,width-24,height-68,TRUE);
            MoveWindow(GetDlgItem(window,IDOK),width-260,height-44,130,30,TRUE);
            MoveWindow(GetDlgItem(window,IDCANCEL),width-120,height-44,108,30,TRUE);return 0;
        }
        if(message==WM_GETMINMAXINFO){auto info=reinterpret_cast<MINMAXINFO*>(l);info->ptMinTrackSize={480,320};return 0;}
        if(message==WM_COMMAND && (LOWORD(w)==IDOK || LOWORD(w)==IDCANCEL)){state->accepted=LOWORD(w)==IDOK;DestroyWindow(window);return 0;}
        if(message==WM_CLOSE){DestroyWindow(window);return 0;}
        return DefWindowProc(window,message,w,l);
    }
    bool Confirm(HWND owner,const Workflow::Json& preview,const Workflow::Json& document)
    {
        auto map=document.at("map").get<std::string>();if(map.rfind("unsaved:",0)==0)map="Unsaved map (current editor session)";
        std::string text="Map: "+map+"\r\n"+document.value("description",std::string{})+"\r\n\r\n"+preview.at("note").get<std::string>()+"\r\n\r\n";
        for(const auto& line:preview.at("changes"))text+=line.get<std::string>()+"\r\n";
        Preview state{Wide(text)};
        WNDCLASSW cls{};cls.lpfnWndProc=PreviewProc;cls.hInstance=GetModuleHandle(nullptr);cls.lpszClassName=L"ReloadedMapAuthoringPreview";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);RegisterClassW(&cls);
        auto window=CreateWindowExW(WS_EX_DLGMODALFRAME,cls.lpszClassName,L"Preview Map JSON Import",WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,CW_USEDEFAULT,850,650,owner,nullptr,cls.hInstance,&state);
        if(!window)throw std::runtime_error("Could not open the change preview.");
        EnableWindow(owner,FALSE);ShowWindow(window,SW_SHOW);MSG message{};
        while(IsWindow(window))
        {
            auto status=GetMessage(&message,nullptr,0,0);
            if(status<=0){if(status==0)PostQuitMessage(static_cast<int>(message.wParam));break;}
            if(!IsDialogMessage(window,&message)){TranslateMessage(&message);DispatchMessage(&message);}
        }
        if(IsWindow(window))DestroyWindow(window);EnableWindow(owner,TRUE);SetActiveWindow(owner);return state.accepted;
    }
}
void Open(HWND owner,bool exporting)
{
    try
    {
        wchar_t path[32768]{};wcscpy_s(path,exporting?L"map.json":L"map-changes.json");
        OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=owner;dialog.lpstrFilter=L"Map JSON (*.json)\0*.json\0\0";
        dialog.lpstrFile=path;dialog.nMaxFile=32768;dialog.lpstrDefExt=L"json";dialog.lpstrTitle=exporting?L"Export Map to JSON":L"Import Map from JSON";
        dialog.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(exporting?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
        if(!(exporting?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog))){if(CommDlgExtendedError())throw std::runtime_error("File dialog failed.");return;}
        if(exporting)
        {
            Workflow::WriteDocument(std::filesystem::path(path),Workflow::Editor::ExportMapAuthoring());
            MessageBoxW(owner,L"Map exported to JSON. The file contains actor properties, available classes and assets, geometry context, and a template for importing changes.",L"Map JSON",MB_OK);return;
        }
        if(std::filesystem::file_size(path)>128*1024*1024)throw std::runtime_error("Change file exceeds 128 MiB.");
        std::ifstream input(std::filesystem::path(path),std::ios::binary);if(!input)throw std::runtime_error("Cannot read the change file.");
        auto document=Workflow::Json::parse(input,[](int depth,Workflow::Json::parse_event_t,Workflow::Json&){if(depth>64)throw std::runtime_error("Change file is nested too deeply.");return true;});
        auto preview=Workflow::Editor::PreviewMapAuthoring(document);
        const auto level=Workflow::Editor::LevelIdentity();const auto generation=Workflow::Editor::MapGeneration();const auto revision=Workflow::Editor::Revision();
        if(!Confirm(owner,preview,document))return;
        if(level!=Workflow::Editor::LevelIdentity() || generation!=Workflow::Editor::MapGeneration() || revision!=Workflow::Editor::Revision())throw std::runtime_error("Map changed during preview. Import again to review the current changes.");
        Workflow::Editor::ApplyMapAuthoring(document);
        MessageBoxW(owner,L"Map JSON changes imported. Undo reverses the batch in one step. Save the map to keep the changes.",L"Map JSON",MB_OK);
    }
    catch(const std::exception& e){MessageBoxW(owner,Wide(e.what()).c_str(),L"Map JSON",MB_OK|MB_ICONERROR);}
}
}
