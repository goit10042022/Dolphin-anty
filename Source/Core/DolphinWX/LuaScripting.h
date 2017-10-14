// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <wx/artprov.h>
#include <wx/string.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/menu.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/frame.h>
#include <wx/filedlg.h>
#include <map>
#include <mutex>
#include <math.h>
#include "InputCommon\GCPadStatus.h"
#include "Core\HW\ProcessorInterface.h"
#include "Core\State.h"

//Lua includes
#ifdef __cplusplus
#include "lua5.3\include\lua.hpp"
#else
#include <lua5.3\include\lua.h>
#include <lua5.3\include\lualib.h>
#include <lua5.3\include\laux.h>
#endif

typedef int(*LuaFunction)(lua_State* L);

namespace Lua
{
  extern std::map<const char*, LuaFunction>* registered_functions;

  // pad_status is shared between the window thread and the script executing thread.
  // so access to it must be mutex protected.
  extern GCPadStatus* pad_status;
  extern std::mutex lua_mutex;

  void ClearPad(GCPadStatus*);

  class LuaScriptFrame;

  class LuaThread : public wxThread
  {
  public:
    LuaThread(LuaScriptFrame* p, wxString file);
    ~LuaThread();

    wxThread::ExitCode Entry();

  private:
    LuaScriptFrame* parent = nullptr;
    wxString file_path;
  };

  class LuaScriptFrame final : public wxFrame
  {
  public:
    void Log(const char* message);
    void GetValues(GCPadStatus* status);
    void NullifyLuaThread();

    LuaScriptFrame(wxWindow* parent);

    ~LuaScriptFrame();

  private:
    void CreateGUI();
    void OnClearClicked(wxCommandEvent& event);
    void OnDocumentationClicked(wxCommandEvent& event);
    void OnAPIClicked(wxCommandEvent& event);
    void BrowseOnButtonClick(wxCommandEvent& event);
    void RunOnButtonClick(wxCommandEvent& event);
    void StopOnButtonClick(wxCommandEvent& event);
    wxMenuBar* m_menubar;
    wxMenuItem* clear;
    wxMenuItem* documentation;
    wxMenuItem* api;
    wxMenu* console_menu;
    wxMenu* help_menu;
    wxStaticText* script_file_label;
    wxTextCtrl* file_path;
    wxButton* browse_button;
    wxButton* run_button;
    wxButton* stop_button;
    wxStaticText* output_console_literal;
    wxTextCtrl* output_console;
    LuaThread* lua_thread;
  };
}
