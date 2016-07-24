// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <string>
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include "Common/BreakPoints.h"
#include "Common/CommonTypes.h"
#include "Common/StringUtil.h"
#include "Core/PowerPC/PowerPC.h"
#include "DolphinWX/Debugger/BreakpointWindow.h"
#include "DolphinWX/Debugger/MemoryCheckDlg.h"
#include "DolphinWX/WxUtils.h"

#define TEXT_BOX(text) new wxStaticText(this, wxID_ANY, text)

MemoryCheckDlg::MemoryCheckDlg(CBreakPointWindow* parent)
    : wxDialog(parent, wxID_ANY, _("Memory Check")), m_parent(parent)
{
  Bind(wxEVT_BUTTON, &MemoryCheckDlg::OnOK, this, wxID_OK);

  m_pEditStartAddress = new wxTextCtrl(this, wxID_ANY, "");
  m_pEditEndAddress = new wxTextCtrl(this, wxID_ANY, "");
  m_pWriteFlag = new wxCheckBox(this, wxID_ANY, _("Write"));
  m_pWriteFlag->SetValue(true);
  m_pReadFlag = new wxCheckBox(this, wxID_ANY, _("Read"));

  m_log_flag = new wxCheckBox(this, wxID_ANY, _("Log"));
  m_log_flag->SetValue(true);
  m_break_flag = new wxCheckBox(this, wxID_ANY, _("Break"));

  const int space5 = FromDIP(5);
  const int space10 = FromDIP(10);

  wxStaticBoxSizer* sAddressRangeBox = new wxStaticBoxSizer(wxHORIZONTAL, this, _("Address Range"));
  sAddressRangeBox->Add(TEXT_BOX(_("Start")), 0, wxALIGN_CENTER_VERTICAL);
  sAddressRangeBox->Add(m_pEditStartAddress, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, space5);
  sAddressRangeBox->Add(TEXT_BOX(_("End")), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, space10);
  sAddressRangeBox->Add(m_pEditEndAddress, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, space5);

  wxStaticBoxSizer* sActionBox = new wxStaticBoxSizer(wxVERTICAL, this, _("Action"));
  sActionBox->Add(m_pWriteFlag);
  sActionBox->Add(m_pReadFlag);

  wxBoxSizer* sFlags = new wxStaticBoxSizer(wxVERTICAL, this, _("Flags"));
  sFlags->Add(m_log_flag);
  sFlags->Add(m_break_flag);

  wxBoxSizer* sControls = new wxBoxSizer(wxHORIZONTAL);
  sControls->Add(sAddressRangeBox, 0, wxEXPAND);
  sControls->Add(sActionBox, 0, wxEXPAND);
  sControls->Add(sFlags, 0, wxEXPAND);

  wxBoxSizer* sMainSizer = new wxBoxSizer(wxVERTICAL);
  sMainSizer->AddSpacer(space5);
  sMainSizer->Add(sControls, 0, wxEXPAND | wxLEFT | wxRIGHT, space5);
  sMainSizer->AddSpacer(space5);
  sMainSizer->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxLEFT | wxRIGHT, space5);
  sMainSizer->AddSpacer(space5);

  SetSizerAndFit(sMainSizer);
  SetFocus();
}

void MemoryCheckDlg::OnOK(wxCommandEvent& event)
{
  wxString StartAddressString = m_pEditStartAddress->GetLineText(0);
  wxString EndAddressString = m_pEditEndAddress->GetLineText(0);
  bool OnRead = m_pReadFlag->GetValue();
  bool OnWrite = m_pWriteFlag->GetValue();
  bool Log = m_log_flag->GetValue();
  bool Break = m_break_flag->GetValue();

  u32 StartAddress, EndAddress;
  bool EndAddressOK =
      EndAddressString.Len() && AsciiToHex(WxStrToStr(EndAddressString), EndAddress);

  if (AsciiToHex(WxStrToStr(StartAddressString), StartAddress) && (OnRead || OnWrite) &&
      (Log || Break))
  {
    TMemCheck MemCheck;

    if (!EndAddressOK)
      EndAddress = StartAddress;

    MemCheck.StartAddress = StartAddress;
    MemCheck.EndAddress = EndAddress;
    MemCheck.bRange = StartAddress != EndAddress;
    MemCheck.OnRead = OnRead;
    MemCheck.OnWrite = OnWrite;
    MemCheck.Log = Log;
    MemCheck.Break = Break;

    PowerPC::memchecks.Add(MemCheck);
    m_parent->NotifyUpdate();
    Close();
  }

  event.Skip();
}
