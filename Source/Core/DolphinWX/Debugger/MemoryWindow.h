// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <wx/panel.h>
#include "Common/CommonTypes.h"

class CMemoryView;
class IniFile;
class wxButton;
class wxRadioButton;
class wxListBox;
class wxSearchCtrl;
class wxStaticText;
class wxTextCtrl;

class CMemoryWindow : public wxPanel
{
public:
  CMemoryWindow(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL | wxBORDER_NONE,
                const wxString& name = _("Memory"));

  void Repopulate();

  void JumpToAddress(u32 _Address);

private:
  DECLARE_EVENT_TABLE()

  void OnDataTypeChanged(wxCommandEvent& event);
  void OnSearch(wxCommandEvent& event);
  void OnAddrBoxChange(wxCommandEvent& event);
  void OnValueChanged(wxCommandEvent&);
  void SetMemoryValueFromValBox(wxCommandEvent& event);
  void SetMemoryValue(wxCommandEvent& event);
  void OnDumpMemory(wxCommandEvent& event);
  void OnDumpMem2(wxCommandEvent& event);
  void OnDumpFakeVMEM(wxCommandEvent& event);

  wxButton* btnSearch;
  wxRadioButton* m_rb_ascii;
  wxRadioButton* m_rb_hex;
  wxStaticText* m_search_result_msg;

  CMemoryView* memview;

  wxSearchCtrl* addrbox;
  wxTextCtrl* valbox;

  u32 m_last_search_address = 0;
  bool m_continue_search = false;
};
