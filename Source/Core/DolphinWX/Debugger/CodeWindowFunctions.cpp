// Copyright 2009 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <cstddef>
#include <istream>
#include <string>
#include <utility>
#include <vector>
#include <wx/filedlg.h>
#include <wx/font.h>
#include <wx/fontdata.h>
#include <wx/fontdlg.h>
#include <wx/listbox.h>
#include <wx/menu.h>
#include <wx/mimetype.h>
#include <wx/textdlg.h>

#include "Common/CommonPaths.h"
#include "Common/CommonTypes.h"
#include "Common/FileUtil.h"
#include "Common/IniFile.h"
#include "Common/SymbolDB.h"

#include "Core/Boot/Boot.h"
#include "Core/Core.h"
#include "Core/HLE/HLE.h"
#include "Core/Host.h"
#include "Core/PowerPC/JitCommon/JitBase.h"
#include "Core/PowerPC/PPCAnalyst.h"
#include "Core/PowerPC/PPCSymbolDB.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/PowerPC/Profiler.h"
#include "Core/PowerPC/SignatureDB.h"

#include "DolphinWX/Debugger/BreakpointWindow.h"
#include "DolphinWX/Debugger/CodeWindow.h"
#include "DolphinWX/Debugger/DSPDebugWindow.h"
#include "DolphinWX/Debugger/DebuggerPanel.h"
#include "DolphinWX/Debugger/DebuggerUIUtil.h"
#include "DolphinWX/Debugger/JitWindow.h"
#include "DolphinWX/Debugger/MemoryWindow.h"
#include "DolphinWX/Debugger/RegisterWindow.h"
#include "DolphinWX/Debugger/WatchWindow.h"
#include "DolphinWX/Frame.h"
#include "DolphinWX/Globals.h"
#include "DolphinWX/WxUtils.h"

// Save and load settings
// -----------------------------
void CCodeWindow::Load()
{
  IniFile ini;
  ini.Load(File::GetUserPath(F_DEBUGGERCONFIG_IDX));

  // The font to override DebuggerFont with
  std::string fontDesc;

  IniFile::Section* general = ini.GetOrCreateSection("General");
  general->Get("DebuggerFont", &fontDesc);
  general->Get("AutomaticStart", &bAutomaticStart, false);
  general->Get("BootToPause", &bBootToPause, true);

  if (!fontDesc.empty())
    DebuggerFont.SetNativeFontInfoUserDesc(StrToWxStr(fontDesc));

  const char* SettingName[] = {"Log",    "LogConfig", "Console", "Registers", "Breakpoints",
                               "Memory", "JIT",       "Sound",   "Video",     "Code"};

  // Decide what windows to show
  for (int i = 0; i <= IDM_VIDEO_WINDOW - IDM_LOG_WINDOW; i++)
    ini.GetOrCreateSection("ShowOnStart")->Get(SettingName[i], &bShowOnStart[i], false);

  // Get notebook affiliation
  std::string section = "P - " + ((Parent->ActivePerspective < Parent->Perspectives.size()) ?
                                      Parent->Perspectives[Parent->ActivePerspective].Name :
                                      "Perspective 1");

  for (int i = 0; i <= IDM_CODE_WINDOW - IDM_LOG_WINDOW; i++)
    ini.GetOrCreateSection(section)->Get(SettingName[i], &iNbAffiliation[i], 0);

  // Get floating setting
  for (int i = 0; i <= IDM_CODE_WINDOW - IDM_LOG_WINDOW; i++)
    ini.GetOrCreateSection("Float")->Get(SettingName[i], &Parent->bFloatWindow[i], false);
}

void CCodeWindow::Save()
{
  IniFile ini;
  ini.Load(File::GetUserPath(F_DEBUGGERCONFIG_IDX));

  IniFile::Section* general = ini.GetOrCreateSection("General");
  general->Set("DebuggerFont", WxStrToStr(DebuggerFont.GetNativeFontInfoUserDesc()));
  general->Set("AutomaticStart", GetMenuBar()->IsChecked(IDM_AUTOMATIC_START));
  general->Set("BootToPause", GetMenuBar()->IsChecked(IDM_BOOT_TO_PAUSE));

  const char* SettingName[] = {"Log",    "LogConfig", "Console", "Registers", "Breakpoints",
                               "Memory", "JIT",       "Sound",   "Video",     "Code"};

  // Save windows settings
  for (int i = IDM_LOG_WINDOW; i <= IDM_VIDEO_WINDOW; i++)
    ini.GetOrCreateSection("ShowOnStart")
        ->Set(SettingName[i - IDM_LOG_WINDOW], GetMenuBar()->IsChecked(i));

  // Save notebook affiliations
  std::string section = "P - " + Parent->Perspectives[Parent->ActivePerspective].Name;
  for (int i = 0; i <= IDM_CODE_WINDOW - IDM_LOG_WINDOW; i++)
    ini.GetOrCreateSection(section)->Set(SettingName[i], iNbAffiliation[i]);

  // Save floating setting
  for (int i = IDM_LOG_WINDOW_PARENT; i <= IDM_CODE_WINDOW_PARENT; i++)
    ini.GetOrCreateSection("Float")->Set(SettingName[i - IDM_LOG_WINDOW_PARENT],
                                         !!FindWindowById(i));

  ini.Save(File::GetUserPath(F_DEBUGGERCONFIG_IDX));
}

// Symbols, JIT, Profiler
// ----------------
void CCodeWindow::CreateMenuSymbols(wxMenuBar* pMenuBar)
{
  wxMenu* pSymbolsMenu = new wxMenu;
  pSymbolsMenu->Append(IDM_CLEAR_SYMBOLS, _("&Clear Symbols"),
                       _("Remove names from all functions and variables."));
  pSymbolsMenu->Append(IDM_SCAN_FUNCTIONS, _("&Generate Symbol Map"),
                       _("Recognise standard functions from sys\\totaldb.dsy, and use generic zz_ "
                         "names for other functions."));
  pSymbolsMenu->AppendSeparator();
  pSymbolsMenu->Append(IDM_LOAD_MAP_FILE, _("&Load Symbol Map"),
                       _("Try to load this game's function names automatically - but doesn't check "
                         ".map files stored on the disc image yet."));
  pSymbolsMenu->Append(IDM_SAVEMAPFILE, _("&Save Symbol Map"),
                       _("Save the function names for each address to a .map file in your user "
                         "settings map folder, named after the title id."));
  pSymbolsMenu->AppendSeparator();
  pSymbolsMenu->Append(
      IDM_LOAD_MAP_FILE_AS, _("Load &Other Map File..."),
      _("Load any .map file containing the function names and addresses for this game."));
  pSymbolsMenu->Append(
      IDM_LOAD_BAD_MAP_FILE, _("Load &Bad Map File..."),
      _("Try to load a .map file that might be from a slightly different version."));
  pSymbolsMenu->Append(IDM_SAVE_MAP_FILE_AS, _("Save Symbol Map &As..."),
                       _("Save the function names and addresses for this game as a .map file. If "
                         "you want to open it in IDA pro, use the .idc script."));
  pSymbolsMenu->AppendSeparator();
  pSymbolsMenu->Append(
      IDM_SAVE_MAP_FILE_WITH_CODES, _("Save Code"),
      _("Save the entire disassembled code. This may take a several seconds"
        " and may require between 50 and 100 MB of hard drive space. It will only save code"
        " that are in the first 4 MB of memory, if you are debugging a game that load .rel"
        " files with code to memory you may want to increase that to perhaps 8 MB, you can do"
        " that from SymbolDB::SaveMap()."));

  pSymbolsMenu->AppendSeparator();
  pSymbolsMenu->Append(
      IDM_CREATE_SIGNATURE_FILE, _("&Create Signature File..."),
      _("Create a .dsy file that can be used to recognise these same functions in other games."));
  pSymbolsMenu->Append(IDM_APPEND_SIGNATURE_FILE, _("Append to &Existing Signature File..."),
                       _("Add any named functions missing from a .dsy file, so it can also "
                         "recognise these additional functions in other games."));
  pSymbolsMenu->Append(IDM_COMBINE_SIGNATURE_FILES, _("Combine Two Signature Files..."),
                       _("Make a new .dsy file which can recognise more functions, by combining "
                         "two existing files. The first input file has priority."));
  pSymbolsMenu->Append(
      IDM_USE_SIGNATURE_FILE, _("Apply Signat&ure File..."),
      _("Must use Generate symbol map first! Recognise names of any standard library functions "
        "used in multiple games, by loading them from a .dsy file."));
  pSymbolsMenu->AppendSeparator();
  pSymbolsMenu->Append(IDM_PATCH_HLE_FUNCTIONS, _("&Patch HLE Functions"));
  pSymbolsMenu->Append(IDM_RENAME_SYMBOLS, _("&Rename Symbols from File..."));
  pMenuBar->Append(pSymbolsMenu, _("&Symbols"));

  wxMenu* pProfilerMenu = new wxMenu;
  pProfilerMenu->Append(IDM_PROFILE_BLOCKS, _("&Profile Blocks"), wxEmptyString, wxITEM_CHECK);
  pProfilerMenu->AppendSeparator();
  pProfilerMenu->Append(IDM_WRITE_PROFILE, _("&Write to profile.txt, Show"));
  pMenuBar->Append(pProfilerMenu, _("&Profiler"));
}

void CCodeWindow::OnProfilerMenu(wxCommandEvent& event)
{
  switch (event.GetId())
  {
  case IDM_PROFILE_BLOCKS:
    Core::SetState(Core::CORE_PAUSE);
    if (jit != nullptr)
      jit->ClearCache();
    Profiler::g_ProfileBlocks = GetMenuBar()->IsChecked(IDM_PROFILE_BLOCKS);
    Core::SetState(Core::CORE_RUN);
    break;
  case IDM_WRITE_PROFILE:
    if (Core::GetState() == Core::CORE_RUN)
      Core::SetState(Core::CORE_PAUSE);

    if (Core::GetState() == Core::CORE_PAUSE && PowerPC::GetMode() == PowerPC::MODE_JIT)
    {
      if (jit != nullptr)
      {
        std::string filename = File::GetUserPath(D_DUMP_IDX) + "Debug/profiler.txt";
        File::CreateFullPath(filename);
        Profiler::WriteProfileResults(filename);

        wxFileType* filetype = nullptr;
        if (!(filetype = wxTheMimeTypesManager->GetFileTypeFromExtension("txt")))
        {
          // From extension failed, trying with MIME type now
          if (!(filetype = wxTheMimeTypesManager->GetFileTypeFromMimeType("text/plain")))
            // MIME type failed, aborting mission
            break;
        }
        wxString OpenCommand;
        OpenCommand = filetype->GetOpenCommand(StrToWxStr(filename));
        if (!OpenCommand.IsEmpty())
          wxExecute(OpenCommand, wxEXEC_SYNC);
      }
    }
    break;
  }
}

void CCodeWindow::OnSymbolsMenu(wxCommandEvent& event)
{
  Parent->ClearStatusBar();

  if (!Core::IsRunning())
    return;

  std::string existing_map_file, writable_map_file, title_id_str;
  bool map_exists = CBoot::FindMapFile(&existing_map_file, &writable_map_file, &title_id_str);
  switch (event.GetId())
  {
  case IDM_CLEAR_SYMBOLS:
    if (!AskYesNoT("Do you want to clear the list of symbol names?"))
      return;
    g_symbolDB.Clear();
    Host_NotifyMapLoaded();
    break;
  case IDM_SCAN_FUNCTIONS:
  {
    PPCAnalyst::FindFunctions(0x80000000, 0x81800000, &g_symbolDB);
    SignatureDB db;
    if (db.Load(File::GetSysDirectory() + TOTALDB))
    {
      db.Apply(&g_symbolDB);
      Parent->StatusBarMessage("Generated symbol names from '%s'", TOTALDB);
      db.List();
    }
    else
    {
      Parent->StatusBarMessage("'%s' not found, no symbol names generated", TOTALDB);
    }
    // HLE::PatchFunctions();
    // Update GUI
    NotifyMapLoaded();
    break;
  }
  case IDM_LOAD_MAP_FILE:
    if (!map_exists)
    {
      g_symbolDB.Clear();
      PPCAnalyst::FindFunctions(0x81300000, 0x81800000, &g_symbolDB);
      SignatureDB db;
      if (db.Load(File::GetSysDirectory() + TOTALDB))
        db.Apply(&g_symbolDB);
      Parent->StatusBarMessage("'%s' not found, scanning for common functions instead",
                               writable_map_file.c_str());
    }
    else
    {
      g_symbolDB.LoadMap(existing_map_file);
      Parent->StatusBarMessage("Loaded symbols from '%s'", existing_map_file.c_str());
    }
    HLE::PatchFunctions();
    NotifyMapLoaded();
    break;
  case IDM_LOAD_MAP_FILE_AS:
  {
    const wxString path = wxFileSelector(
        _("Load map file"), File::GetUserPath(D_MAPS_IDX), title_id_str + ".map", ".map",
        _("Dolphin Map File (*.map)") + "|*.map|" + wxGetTranslation(wxALL_FILES),
        wxFD_OPEN | wxFD_FILE_MUST_EXIST, this);

    if (!path.IsEmpty())
    {
      g_symbolDB.LoadMap(WxStrToStr(path));
      Parent->StatusBarMessage("Loaded symbols from '%s'", WxStrToStr(path).c_str());
    }
    HLE::PatchFunctions();
    NotifyMapLoaded();
  }
  break;
  case IDM_LOAD_BAD_MAP_FILE:
  {
    const wxString path = wxFileSelector(
        _("Load bad map file"), File::GetUserPath(D_MAPS_IDX), title_id_str + ".map", ".map",
        _("Dolphin Map File (*.map)") + "|*.map|" + wxGetTranslation(wxALL_FILES),
        wxFD_OPEN | wxFD_FILE_MUST_EXIST, this);

    if (!path.IsEmpty())
    {
      g_symbolDB.LoadMap(WxStrToStr(path), true);
      Parent->StatusBarMessage("Loaded symbols from '%s'", WxStrToStr(path).c_str());
    }
    HLE::PatchFunctions();
    NotifyMapLoaded();
  }
  break;
  case IDM_SAVEMAPFILE:
    g_symbolDB.SaveMap(writable_map_file);
    break;
  case IDM_SAVE_MAP_FILE_AS:
  {
    const wxString path = wxFileSelector(
        _("Save map file as"), File::GetUserPath(D_MAPS_IDX), title_id_str + ".map", ".map",
        _("Dolphin Map File (*.map)") + "|*.map|" + wxGetTranslation(wxALL_FILES),
        wxFD_SAVE | wxFD_OVERWRITE_PROMPT, this);

    if (!path.IsEmpty())
      g_symbolDB.SaveMap(WxStrToStr(path));
  }
  break;
  case IDM_SAVE_MAP_FILE_WITH_CODES:
    g_symbolDB.SaveMap(writable_map_file, true);
    break;

  case IDM_RENAME_SYMBOLS:
  {
    const wxString path = wxFileSelector(
        _("Apply signature file"), wxEmptyString, wxEmptyString, wxEmptyString,
        _("Dolphin Symbol Rename File (*.sym)") + "|*.sym|" + wxGetTranslation(wxALL_FILES),
        wxFD_OPEN | wxFD_FILE_MUST_EXIST, this);

    if (!path.IsEmpty())
    {
      std::ifstream f;
      OpenFStream(f, WxStrToStr(path), std::ios_base::in);

      std::string line;
      while (std::getline(f, line))
      {
        if (line.length() < 12)
          continue;

        u32 address, type;
        std::string name;

        std::istringstream ss(line);
        ss >> std::hex >> address >> std::dec >> type >> name;

        Symbol* symbol = g_symbolDB.GetSymbolFromAddr(address);
        if (symbol)
          symbol->name = line.substr(12);
      }

      Host_NotifyMapLoaded();
    }
  }
  break;

  case IDM_CREATE_SIGNATURE_FILE:
  {
    wxTextEntryDialog input_prefix(this,
                                   _("Only export symbols with prefix:\n(Blank for all symbols)"),
                                   wxGetTextFromUserPromptStr, wxEmptyString);

    if (input_prefix.ShowModal() == wxID_OK)
    {
      std::string prefix(WxStrToStr(input_prefix.GetValue()));

      wxString path = wxFileSelector(_("Save signature as"), File::GetSysDirectory(), wxEmptyString,
                                     wxEmptyString, _("Dolphin Signature File (*.dsy)") +
                                                        "|*.dsy|" + wxGetTranslation(wxALL_FILES),
                                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT, this);
      if (!path.IsEmpty())
      {
        SignatureDB db;
        db.Initialize(&g_symbolDB, prefix);
        db.Save(WxStrToStr(path));
        db.List();
      }
    }
  }
  break;
  case IDM_APPEND_SIGNATURE_FILE:
  {
    wxTextEntryDialog input_prefix(this,
                                   _("Only export symbols with prefix:\n(Blank for all symbols)"),
                                   wxGetTextFromUserPromptStr, wxEmptyString);

    if (input_prefix.ShowModal() == wxID_OK)
    {
      std::string prefix(WxStrToStr(input_prefix.GetValue()));

      wxString path = wxFileSelector(
          _("Append signature to"), File::GetSysDirectory(), wxEmptyString, wxEmptyString,
          _("Dolphin Signature File (*.dsy)") + "|*.dsy|" + wxGetTranslation(wxALL_FILES),
          wxFD_SAVE, this);
      if (!path.IsEmpty())
      {
        SignatureDB db;
        db.Initialize(&g_symbolDB, prefix);
        db.List();
        db.Load(WxStrToStr(path));
        db.Save(WxStrToStr(path));
        db.List();
      }
    }
  }
  break;
  case IDM_USE_SIGNATURE_FILE:
  {
    wxString path = wxFileSelector(
        _("Apply signature file"), File::GetSysDirectory(), wxEmptyString, wxEmptyString,
        _("Dolphin Signature File (*.dsy)") + "|*.dsy|" + wxGetTranslation(wxALL_FILES),
        wxFD_OPEN | wxFD_FILE_MUST_EXIST, this);
    if (!path.IsEmpty())
    {
      SignatureDB db;
      db.Load(WxStrToStr(path));
      db.Apply(&g_symbolDB);
      db.List();
      NotifyMapLoaded();
    }
  }
  break;
  case IDM_COMBINE_SIGNATURE_FILES:
  {
    wxString path1 = wxFileSelector(
        _("Choose priority input file"), File::GetSysDirectory(), wxEmptyString, wxEmptyString,
        _("Dolphin Signature File (*.dsy)") + "|*.dsy|" + wxGetTranslation(wxALL_FILES),
        wxFD_OPEN | wxFD_FILE_MUST_EXIST, this);
    if (!path1.IsEmpty())
    {
      SignatureDB db;
      wxString path2 = wxFileSelector(
          _("Choose secondary input file"), File::GetSysDirectory(), wxEmptyString, wxEmptyString,
          _("Dolphin Signature File (*.dsy)") + "|*.dsy|" + wxGetTranslation(wxALL_FILES),
          wxFD_OPEN | wxFD_FILE_MUST_EXIST, this);
      if (!path2.IsEmpty())
      {
        db.Load(WxStrToStr(path2));
        db.Load(WxStrToStr(path1));

        path2 = wxFileSelector(_("Save combined output file as"), File::GetSysDirectory(),
                               wxEmptyString, ".dsy", _("Dolphin Signature File (*.dsy)") +
                                                          "|*.dsy|" + wxGetTranslation(wxALL_FILES),
                               wxFD_SAVE | wxFD_OVERWRITE_PROMPT, this);
        db.Save(WxStrToStr(path2));
        db.List();
      }
    }
  }
  break;
  case IDM_PATCH_HLE_FUNCTIONS:
    HLE::PatchFunctions();
    Repopulate();
    break;
  }
}

void CCodeWindow::NotifyMapLoaded()
{
  if (!codeview)
    return;

  g_symbolDB.FillInCallers();
  symbols->Freeze();  // HyperIris: wx style fast filling
  symbols->Clear();
  for (const auto& symbol : g_symbolDB.Symbols())
  {
    int idx = symbols->Append(StrToWxStr(symbol.second.name));
    symbols->SetClientData(idx, (void*)&symbol.second);
  }
  symbols->Thaw();
  Repopulate();
}

void CCodeWindow::OnSymbolListChange(wxCommandEvent& event)
{
  int index = symbols->GetSelection();
  if (index >= 0)
  {
    Symbol* pSymbol = static_cast<Symbol*>(symbols->GetClientData(index));
    if (pSymbol != nullptr)
    {
      if (pSymbol->type == Symbol::SYMBOL_DATA)
      {
        CMemoryWindow* memory = GetPanel<CMemoryWindow>();
        if (memory)
          memory->JumpToAddress(pSymbol->address);
      }
      else
      {
        JumpToAddress(pSymbol->address);
      }
    }
  }
}

// Change the global DebuggerFont
void CCodeWindow::OnChangeFont(wxCommandEvent& event)
{
  wxFontData data;
  data.SetInitialFont(DebuggerFont);

  wxFontDialog dialog(this, data);
  if (dialog.ShowModal() == wxID_OK)
    DebuggerFont = dialog.GetFontData().GetChosenFont();

  // TODO: Send event to all panels that tells them to reload the font when it changes.
}

// Toggle windows

wxPanel* CCodeWindow::GetUntypedPanel(int id) const
{
  wxASSERT_MSG(id >= IDM_DEBUG_WINDOW_LIST_START && id < IDM_DEBUG_WINDOW_LIST_END,
               "ID out of range");
  wxASSERT_MSG(id != IDM_LOG_WINDOW && id != IDM_LOG_CONFIG_WINDOW,
               "Log windows are managed separately");
  return m_sibling_panels.at(id - IDM_DEBUG_WINDOW_LIST_START);
}

void CCodeWindow::TogglePanel(int id, bool show)
{
  wxPanel* panel = GetUntypedPanel(id);

  // Not all the panels (i.e. CodeWindow) have corresponding menu options.
  wxMenuItem* item = GetMenuBar()->FindItem(id);
  if (item)
    item->Check(show);

  if (show)
  {
    if (!panel)
    {
      panel = CreateSiblingPanel(id);
    }
    Parent->DoAddPage(panel, iNbAffiliation[id - IDM_DEBUG_WINDOW_LIST_START],
                      Parent->bFloatWindow[id - IDM_DEBUG_WINDOW_LIST_START]);
  }
  else if (panel)  // Close
  {
    Parent->DoRemovePage(panel, panel == this);
    m_sibling_panels[id - IDM_DEBUG_WINDOW_LIST_START] = nullptr;
  }
}

wxPanel* CCodeWindow::CreateSiblingPanel(int id)
{
  // Includes range check inside the get call
  wxASSERT_MSG(!GetUntypedPanel(id), "Panel must not already exist");

  wxPanel* panel = nullptr;
  switch (id)
  {
  // case IDM_LOG_WINDOW:  // These exist separately in CFrame.
  // case IDM_LOG_CONFIG_WINDOW:
  case IDM_REGISTER_WINDOW:
    panel = new CRegisterWindow(Parent, IDM_REGISTER_WINDOW);
    break;
  case IDM_WATCH_WINDOW:
    panel = new CWatchWindow(Parent, IDM_WATCH_WINDOW);
    break;
  case IDM_BREAKPOINT_WINDOW:
    panel = new CBreakPointWindow(this, Parent, IDM_BREAKPOINT_WINDOW);
    break;
  case IDM_MEMORY_WINDOW:
    panel = new CMemoryWindow(Parent, IDM_MEMORY_WINDOW);
    break;
  case IDM_JIT_WINDOW:
    panel = new CJitWindow(Parent, IDM_JIT_WINDOW);
    break;
  case IDM_SOUND_WINDOW:
    panel = new DSPDebuggerLLE(Parent, IDM_SOUND_WINDOW);
    break;
  case IDM_VIDEO_WINDOW:
    panel = new GFXDebuggerPanel(Parent, IDM_VIDEO_WINDOW);
    break;
  case IDM_CODE_WINDOW:
    panel = this;
    break;
  default:
    wxTrap();
    break;
  }

  m_sibling_panels[id - IDM_DEBUG_WINDOW_LIST_START] = panel;
  return panel;
}

void CCodeWindow::OpenPages()
{
  // This is forced, and should always be placed as the first tab in the notebook.
  TogglePanel(IDM_CODE_WINDOW, true);

  // These panels are managed separately by CFrame
  if (bShowOnStart[IDM_LOG_WINDOW - IDM_DEBUG_WINDOW_LIST_START])
    Parent->ToggleLogWindow(true);
  if (bShowOnStart[IDM_LOG_CONFIG_WINDOW - IDM_DEBUG_WINDOW_LIST_START])
    Parent->ToggleLogConfigWindow(true);

  // Iterate normal panels that don't have weird rules.
  for (int i = IDM_REGISTER_WINDOW; i < IDM_CODE_WINDOW; ++i)
  {
    if (bShowOnStart[i - IDM_DEBUG_WINDOW_LIST_START])
      TogglePanel(i, true);
  }
}
