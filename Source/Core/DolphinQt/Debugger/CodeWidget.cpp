// Copyright 2018 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/Debugger/CodeWidget.h"

#include <chrono>

#include <fmt/format.h>

#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QStyleHints>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "Common/Event.h"
#include "Core/Core.h"
#include "Core/Debugger/Debugger_SymbolMap.h"
#include "Core/HW/CPU.h"
#include "Core/PowerPC/MMU.h"
#include "Core/PowerPC/PPCSymbolDB.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"
#include "DolphinQt/Debugger/BranchWatchDialog.h"
#include "DolphinQt/Host.h"
#include "DolphinQt/QtUtils/SetWindowDecorations.h"
#include "DolphinQt/Resources.h"
#include "DolphinQt/Settings.h"

static const QString LOCK_BUTTON_STYLESHEET = QStringLiteral(
    "QToolButton:checked { background-color: rgb(150, 0, 0); border-style: solid;"
    "padding: 0px; border-width: 3px; border-color: rgb(150,0,0); color: rgb(255, 255, 255);}");

static const QString BOX_SPLITTER_STYLESHEET = QStringLiteral(
    "QSplitter::handle { border-top: 1px dashed black; width: 1px; margin-left: 10px; "
    "margin-right: 10px; }");

CodeWidget::CodeWidget(QWidget* parent)
    : QDockWidget(parent), m_system(Core::System::GetInstance()),
      m_diff_dialog(new BranchWatchDialog(m_system, m_system.GetPowerPC().GetBranchWatch(), this))
{
  setWindowTitle(tr("Code"));
  setObjectName(QStringLiteral("code"));

  setHidden(!Settings::Instance().IsCodeVisible() || !Settings::Instance().IsDebugModeEnabled());

  setAllowedAreas(Qt::AllDockWidgetAreas);

  CreateWidgets();

  auto& settings = Settings::GetQSettings();

  restoreGeometry(settings.value(QStringLiteral("codewidget/geometry")).toByteArray());
  // macOS: setHidden() needs to be evaluated before setFloating() for proper window presentation
  // according to Settings
  setFloating(settings.value(QStringLiteral("codewidget/floating")).toBool());

  connect(&Settings::Instance(), &Settings::CodeVisibilityChanged, this,
          [this](bool visible) { setHidden(!visible); });

  connect(Host::GetInstance(), &Host::UpdateDisasmDialog, this, [this] {
    if (Core::GetState() != Core::State::Running)
    {
      if (!m_lock_btn->isChecked())
        SetAddress(m_system.GetPPCState().pc, CodeViewWidget::SetAddressUpdate::WithoutUpdate);
      Update();
    }
  });

  connect(Host::GetInstance(), &Host::NotifyMapLoaded, this, &CodeWidget::UpdateSymbols);

  connect(&Settings::Instance(), &Settings::DebugModeToggled, this,
          [this](bool enabled) { setHidden(!enabled || !Settings::Instance().IsCodeVisible()); });

  connect(&Settings::Instance(), &Settings::EmulationStateChanged, this, [this] {
    if (Core::GetState() == Core::State::Paused)
    {
      if (!m_lock_btn->isChecked())
        SetAddress(m_system.GetPPCState().pc, CodeViewWidget::SetAddressUpdate::WithoutUpdate);
      Update();
    }
  });

  ConnectWidgets();

  m_code_splitter->restoreState(
      settings.value(QStringLiteral("codewidget/codesplitter")).toByteArray());
  m_box_splitter->restoreState(
      settings.value(QStringLiteral("codewidget/boxsplitter")).toByteArray());
}

CodeWidget::~CodeWidget()
{
  auto& settings = Settings::GetQSettings();

  settings.setValue(QStringLiteral("codewidget/geometry"), saveGeometry());
  settings.setValue(QStringLiteral("codewidget/floating"), isFloating());
  settings.setValue(QStringLiteral("codewidget/codesplitter"), m_code_splitter->saveState());
  settings.setValue(QStringLiteral("codewidget/boxsplitter"), m_box_splitter->saveState());
}

void CodeWidget::closeEvent(QCloseEvent*)
{
  Settings::Instance().SetCodeVisible(false);
}

void CodeWidget::showEvent(QShowEvent* event)
{
  Update();
}

void CodeWidget::CreateWidgets()
{
  auto* layout = new QGridLayout;

  layout->setContentsMargins(2, 2, 2, 2);
  layout->setSpacing(0);

  auto* top_layout = new QHBoxLayout;
  m_search_address = new QComboBox;
  m_search_address->setInsertPolicy(QComboBox::InsertAtTop);
  m_search_address->setDuplicatesEnabled(false);
  m_search_address->setEditable(true);
  m_search_address->lineEdit()->setPlaceholderText(tr("Search Address"));
  m_search_address->setMaxVisibleItems(16);
  m_search_address->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
  m_search_address->lineEdit()->setPlaceholderText(tr("Search Address"));

  m_save_address_btn = new QToolButton();
  m_save_address_btn->setIcon(Resources::GetThemeIcon("debugger_save"));
  // 24 is a standard button height.
  m_save_address_btn->setMinimumSize(24, 24);

  m_lock_btn = new QToolButton();
  m_lock_btn->setIcon(Resources::GetThemeIcon("pause"));
  m_lock_btn->setCheckable(true);
  m_lock_btn->setMinimumSize(24, 24);
  m_lock_btn->setStyleSheet(LOCK_BUTTON_STYLESHEET);

  m_branch_watch_dialog = new QPushButton(tr("Branch Watch"));

  top_layout->addWidget(m_search_address);
  top_layout->addWidget(m_save_address_btn);
  top_layout->addWidget(m_lock_btn);
  top_layout->addWidget(m_branch_watch_dialog);

  auto* right_layout = new QVBoxLayout;
  m_code_view = new CodeViewWidget;
  right_layout->addLayout(top_layout);
  right_layout->addWidget(m_code_view);

  m_box_splitter = new QSplitter(Qt::Vertical);
  m_box_splitter->setStyleSheet(BOX_SPLITTER_STYLESHEET);

  auto add_search_line_edit = [this](const QString& name, QListWidget* list_widget) {
    auto* widget = new QWidget;
    auto* line_layout = new QGridLayout;
    auto* label = new QLabel(name);
    auto* search_line_edit = new QLineEdit;

    widget->setLayout(line_layout);
    line_layout->addWidget(label, 0, 0);
    line_layout->addWidget(search_line_edit, 0, 1);
    line_layout->addWidget(list_widget, 1, 0, -1, -1);
    m_box_splitter->addWidget(widget);
    return search_line_edit;
  };

  // Callstack
  m_callstack_list = new QListWidget;
  m_search_callstack = add_search_line_edit(tr("Callstack"), m_callstack_list);

  // Symbols
  m_search_symbols = new QLineEdit;
  auto* s_label = new QLabel(tr("Symbols"));

  auto* symbols_tab = new QTabWidget;
  m_symbols_list = new QListWidget;
  m_note_list = new QListWidget;
  symbols_tab->addTab(m_symbols_list, tr("Symbols"));
  symbols_tab->addTab(m_note_list, tr("Notes"));

  auto* s_layout = new QGridLayout;
  auto* s_widget = new QWidget;
  s_widget->setLayout(s_layout);
  s_layout->addWidget(s_label, 0, 0);
  s_layout->addWidget(m_search_symbols, 0, 1);
  s_layout->addWidget(symbols_tab, 1, 0, -1, -1);
  m_box_splitter->addWidget(s_widget);

  // Function calls
  m_function_calls_list = new QListWidget;
  m_search_calls = add_search_line_edit(tr("Calls"), m_function_calls_list);

  // Function callers
  m_function_callers_list = new QListWidget;
  m_search_callers = add_search_line_edit(tr("Callers"), m_function_callers_list);

  m_code_splitter = new QSplitter(Qt::Horizontal);

  // Wrap to widget for splitter
  QWidget* right_widget = new QWidget;
  right_widget->setLayout(right_layout);

  m_code_splitter->addWidget(m_box_splitter);
  m_code_splitter->addWidget(right_widget);

  layout->addWidget(m_code_splitter, 0, 0, -1, -1);

  QWidget* widget = new QWidget(this);
  widget->setLayout(layout);
  setWidget(widget);
}

void CodeWidget::ConnectWidgets()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
  connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
          [this](Qt::ColorScheme colorScheme) {
            m_box_splitter->setStyleSheet(BOX_SPLITTER_STYLESHEET);
          });
#endif

  connect(m_search_address, &QComboBox::currentTextChanged, this, &CodeWidget::OnSearchAddress);
  connect(m_search_address, &QComboBox::activated, this, &CodeWidget::OnSearchAddress);
  connect(m_search_symbols, &QLineEdit::textChanged, this, &CodeWidget::OnSearchSymbols);
  connect(m_save_address_btn, &QPushButton::pressed, this, [this]() {
    const QString address = QString::number(m_code_view->GetAddress(), 16);
    if (m_search_address->findText(address) == -1)
      m_search_address->insertItem(0, address);
  });
  connect(m_lock_btn, &QPushButton::toggled, this,
          [this](bool checked) { m_code_view->OnLockAddress(checked); });
  connect(m_search_calls, &QLineEdit::textChanged, this, [this]() {
    if (const Common::Symbol* symbol = g_symbolDB.GetSymbolFromAddr(m_code_view->GetAddress()))
      UpdateFunctionCalls(symbol);
  });
  connect(m_search_callers, &QLineEdit::textChanged, this, [this]() {
    if (const Common::Symbol* symbol = g_symbolDB.GetSymbolFromAddr(m_code_view->GetAddress()))
      UpdateFunctionCallers(symbol);
  });
  connect(m_search_callstack, &QLineEdit::textChanged, this, &CodeWidget::UpdateCallstack);

  connect(m_note_list, &QListWidget::itemPressed, this, &CodeWidget::OnSelectNote);
  connect(m_branch_watch_dialog, &QPushButton::pressed, this, &CodeWidget::OnBranchWatchDialog);

  connect(m_symbols_list, &QListWidget::itemPressed, this, &CodeWidget::OnSelectSymbol);
  connect(m_callstack_list, &QListWidget::itemPressed, this, &CodeWidget::OnSelectCallstack);
  connect(m_function_calls_list, &QListWidget::itemPressed, this,
          &CodeWidget::OnSelectFunctionCalls);
  connect(m_function_callers_list, &QListWidget::itemPressed, this,
          &CodeWidget::OnSelectFunctionCallers);

  connect(m_code_view, &CodeViewWidget::SymbolsChanged, this, [this]() {
    UpdateCallstack();
    UpdateSymbols();
    if (const Common::Symbol* symbol = g_symbolDB.GetSymbolFromAddr(m_code_view->GetAddress()))
    {
      UpdateFunctionCalls(symbol);
      UpdateFunctionCallers(symbol);
    }
  });
  connect(m_code_view, &CodeViewWidget::NotesChanged, this, &CodeWidget::UpdateNotes);
  connect(m_code_view, &CodeViewWidget::BreakpointsChanged, this,
          [this] { emit BreakpointsChanged(); });
  connect(m_code_view, &CodeViewWidget::UpdateCodeWidget, this, &CodeWidget::Update);

  connect(m_code_view, &CodeViewWidget::RequestPPCComparison, this,
          &CodeWidget::RequestPPCComparison);
  connect(m_code_view, &CodeViewWidget::ShowMemory, this, &CodeWidget::ShowMemory);
  connect(m_code_view, &CodeViewWidget::DoAutoStep, this, &CodeWidget::DoAutoStep);
}

void CodeWidget::OnBranchWatchDialog()
{
  m_diff_dialog->setWindowFlag(Qt::WindowMinimizeButtonHint);
  SetQWidgetWindowDecorations(m_diff_dialog);
  m_diff_dialog->open();
  m_diff_dialog->raise();
  m_diff_dialog->activateWindow();
}

void CodeWidget::OnSearchAddress()
{
  bool good = true;
  u32 address = m_search_address->currentText().toUInt(&good, 16);

  QPalette palette;
  QFont font;

  if (!good && !m_search_address->currentText().isEmpty())
  {
    font.setBold(true);
    palette.setColor(QPalette::Text, Qt::red);
  }

  m_search_address->setPalette(palette);
  m_search_address->setFont(font);

  if (good)
    m_code_view->SetAddress(address, CodeViewWidget::SetAddressUpdate::WithUpdate);

  Update();

  m_search_address->setFocus();
}

void CodeWidget::OnSearchSymbols()
{
  m_symbol_filter = m_search_symbols->text();
  UpdateSymbols();
  UpdateNotes();
}

void CodeWidget::OnSelectSymbol()
{
  const auto items = m_symbols_list->selectedItems();
  if (items.isEmpty())
    return;

  const u32 address = items[0]->data(Qt::UserRole).toUInt();
  const Common::Symbol* symbol = g_symbolDB.GetSymbolFromAddr(address);

  m_code_view->SetAddress(address, CodeViewWidget::SetAddressUpdate::WithUpdate);
  UpdateCallstack();
  UpdateFunctionCalls(symbol);
  UpdateFunctionCallers(symbol);

  m_code_view->setFocus();
}

void CodeWidget::OnSelectNote()
{
  const auto items = m_note_list->selectedItems();
  if (items.isEmpty())
    return;

  const u32 address = items[0]->data(Qt::UserRole).toUInt();

  m_code_view->SetAddress(address, CodeViewWidget::SetAddressUpdate::WithUpdate);
}

void CodeWidget::OnSelectCallstack()
{
  const auto items = m_callstack_list->selectedItems();
  if (items.isEmpty())
    return;

  m_code_view->SetAddress(items[0]->data(Qt::UserRole).toUInt(),
                          CodeViewWidget::SetAddressUpdate::WithUpdate);
  Update();
}

void CodeWidget::OnSelectFunctionCalls()
{
  const auto items = m_function_calls_list->selectedItems();
  if (items.isEmpty())
    return;

  m_code_view->SetAddress(items[0]->data(Qt::UserRole).toUInt(),
                          CodeViewWidget::SetAddressUpdate::WithUpdate);
  Update();
}

void CodeWidget::OnSelectFunctionCallers()
{
  const auto items = m_function_callers_list->selectedItems();
  if (items.isEmpty())
    return;

  m_code_view->SetAddress(items[0]->data(Qt::UserRole).toUInt(),
                          CodeViewWidget::SetAddressUpdate::WithUpdate);
  Update();
}

void CodeWidget::SetAddress(u32 address, CodeViewWidget::SetAddressUpdate update)
{
  m_code_view->SetAddress(address, update);

  if (update == CodeViewWidget::SetAddressUpdate::WithUpdate ||
      update == CodeViewWidget::SetAddressUpdate::WithDetailedUpdate)
  {
    Settings::Instance().SetCodeVisible(true);
    raise();
    m_code_view->setFocus();
  }
}

void CodeWidget::Update()
{
  if (!isVisible())
    return;

  const Common::Symbol* symbol = g_symbolDB.GetSymbolFromAddr(m_code_view->GetAddress());

  UpdateCallstack();

  m_code_view->Update();
  m_code_view->setFocus();

  if (!symbol)
    return;

  UpdateFunctionCalls(symbol);
  UpdateFunctionCallers(symbol);
}

void CodeWidget::UpdateCallstack()
{
  m_callstack_list->clear();

  if (Core::GetState() != Core::State::Paused)
    return;

  std::vector<Dolphin_Debugger::CallstackEntry> stack;

  const bool success = [this, &stack] {
    Core::CPUThreadGuard guard(m_system);
    return Dolphin_Debugger::GetCallstack(m_system, guard, stack);
  }();

  if (!success)
  {
    m_callstack_list->addItem(tr("Invalid callstack"));
    return;
  }

  const QString filter = m_search_callstack->text();

  for (const auto& frame : stack)
  {
    const QString name = QString::fromStdString(frame.Name.substr(0, frame.Name.length() - 1));

    if (name.toUpper().indexOf(filter.toUpper()) == -1)
      continue;

    auto* item = new QListWidgetItem(name);
    item->setData(Qt::UserRole, frame.vAddress);
    m_callstack_list->addItem(item);
  }
}

void CodeWidget::UpdateSymbols()
{
  const QString selection = m_symbols_list->selectedItems().isEmpty() ?
                                QString{} :
                                m_symbols_list->selectedItems()[0]->text();
  m_symbols_list->clear();

  for (const auto& symbol : g_symbolDB.Symbols())
  {
    QString name = QString::fromStdString(symbol.second.name);

    auto* item = new QListWidgetItem(name);
    if (name == selection)
      item->setSelected(true);

    // Disable non-function symbols as you can't do anything with them.
    if (symbol.second.type != Common::Symbol::Type::Function)
      item->setFlags(Qt::NoItemFlags);

    item->setData(Qt::UserRole, symbol.second.address);

    if (name.toUpper().indexOf(m_symbol_filter.toUpper()) != -1)
      m_symbols_list->addItem(item);
  }

  m_symbols_list->sortItems();

  m_diff_dialog->UpdateSymbols();
}

void CodeWidget::UpdateNotes()
{
  QString selection = m_note_list->selectedItems().isEmpty() ?
                          QStringLiteral("") :
                          m_note_list->selectedItems()[0]->text();
  m_note_list->clear();

  for (const auto& note : g_symbolDB.Notes())
  {
    QString name = QString::fromStdString(note.second.name);

    auto* item = new QListWidgetItem(name);
    if (name == selection)
      item->setSelected(true);

    item->setData(Qt::UserRole, note.second.address);

    if (name.toUpper().indexOf(m_symbol_filter.toUpper()) != -1)
      m_note_list->addItem(item);
  }

  m_note_list->sortItems();
}

void CodeWidget::UpdateFunctionCalls(const Common::Symbol* symbol)
{
  m_function_calls_list->clear();
  const QString filter = m_search_calls->text();

  for (const auto& call : symbol->calls)
  {
    const u32 addr = call.function;
    const Common::Symbol* call_symbol = g_symbolDB.GetSymbolFromAddr(addr);

    if (call_symbol)
    {
      const QString name =
          QString::fromStdString(fmt::format("> {} ({:08x})", call_symbol->name, addr));

      if (name.toUpper().indexOf(filter.toUpper()) == -1)
        continue;

      auto* item = new QListWidgetItem(name);
      item->setData(Qt::UserRole, addr);
      m_function_calls_list->addItem(item);
    }
  }
}

void CodeWidget::UpdateFunctionCallers(const Common::Symbol* symbol)
{
  m_function_callers_list->clear();
  const QString filter = m_search_callers->text();

  for (const auto& caller : symbol->callers)
  {
    const u32 addr = caller.call_address;
    const Common::Symbol* caller_symbol = g_symbolDB.GetSymbolFromAddr(addr);

    if (caller_symbol)
    {
      const QString name =
          QString::fromStdString(fmt::format("< {} ({:08x})", caller_symbol->name, addr));

      if (name.toUpper().indexOf(filter.toUpper()) == -1)
        continue;

      auto* item = new QListWidgetItem(name);
      item->setData(Qt::UserRole, addr);
      m_function_callers_list->addItem(item);
    }
  }
}

void CodeWidget::Step()
{
  auto& cpu = m_system.GetCPU();

  if (!cpu.IsStepping())
    return;

  Common::Event sync_event;

  auto& power_pc = m_system.GetPowerPC();
  PowerPC::CoreMode old_mode = power_pc.GetMode();
  power_pc.SetMode(PowerPC::CoreMode::Interpreter);
  power_pc.GetBreakPoints().ClearAllTemporary();
  cpu.StepOpcode(&sync_event);
  sync_event.WaitFor(std::chrono::milliseconds(20));
  power_pc.SetMode(old_mode);
  Core::DisplayMessage(tr("Step successful!").toStdString(), 2000);
  // Will get a UpdateDisasmDialog(), don't update the GUI here.

  m_diff_dialog->Update();
}

void CodeWidget::StepOver()
{
  auto& cpu = m_system.GetCPU();

  if (!cpu.IsStepping())
    return;

  const UGeckoInstruction inst = [&] {
    Core::CPUThreadGuard guard(m_system);
    return PowerPC::MMU::HostRead_Instruction(guard, m_system.GetPPCState().pc);
  }();

  if (inst.LK)
  {
    auto& breakpoints = m_system.GetPowerPC().GetBreakPoints();
    breakpoints.ClearAllTemporary();
    breakpoints.Add(m_system.GetPPCState().pc + 4, true);
    cpu.EnableStepping(false);
    Core::DisplayMessage(tr("Step over in progress...").toStdString(), 2000);
  }
  else
  {
    Step();
  }
}

// Returns true on a rfi, blr or on a bclr that evaluates to true.
static bool WillInstructionReturn(Core::System& system, UGeckoInstruction inst)
{
  // Is a rfi instruction
  if (inst.hex == 0x4C000064u)
    return true;
  const auto& ppc_state = system.GetPPCState();
  bool counter = (inst.BO_2 >> 2 & 1) != 0 || (CTR(ppc_state) != 0) != ((inst.BO_2 >> 1 & 1) != 0);
  bool condition = inst.BO_2 >> 4 != 0 || ppc_state.cr.GetBit(inst.BI_2) == (inst.BO_2 >> 3 & 1);
  bool isBclr = inst.OPCD_7 == 0b010011 && (inst.hex >> 1 & 0b10000) != 0;
  return isBclr && counter && condition && !inst.LK_3;
}

void CodeWidget::StepOut()
{
  auto& cpu = m_system.GetCPU();

  if (!cpu.IsStepping())
    return;

  // Keep stepping until the next return instruction or timeout after five seconds
  using clock = std::chrono::steady_clock;
  clock::time_point timeout = clock::now() + std::chrono::seconds(5);

  auto& power_pc = m_system.GetPowerPC();
  auto& ppc_state = power_pc.GetPPCState();
  auto& breakpoints = power_pc.GetBreakPoints();
  {
    Core::CPUThreadGuard guard(m_system);

    breakpoints.ClearAllTemporary();

    PowerPC::CoreMode old_mode = power_pc.GetMode();
    power_pc.SetMode(PowerPC::CoreMode::Interpreter);

    // Loop until either the current instruction is a return instruction with no Link flag
    // or a breakpoint is detected so it can step at the breakpoint. If the PC is currently
    // on a breakpoint, skip it.
    UGeckoInstruction inst = PowerPC::MMU::HostRead_Instruction(guard, ppc_state.pc);
    do
    {
      if (WillInstructionReturn(m_system, inst))
      {
        power_pc.SingleStep();
        break;
      }

      if (inst.LK)
      {
        // Step over branches
        u32 next_pc = ppc_state.pc + 4;
        do
        {
          power_pc.SingleStep();
        } while (ppc_state.pc != next_pc && clock::now() < timeout &&
                 !breakpoints.IsAddressBreakPoint(ppc_state.pc));
      }
      else
      {
        power_pc.SingleStep();
      }

      inst = PowerPC::MMU::HostRead_Instruction(guard, ppc_state.pc);
    } while (clock::now() < timeout && !breakpoints.IsAddressBreakPoint(ppc_state.pc));

    power_pc.SetMode(old_mode);
  }

  emit Host::GetInstance()->UpdateDisasmDialog();

  if (breakpoints.IsAddressBreakPoint(ppc_state.pc))
    Core::DisplayMessage(tr("Breakpoint encountered! Step out aborted.").toStdString(), 2000);
  else if (clock::now() >= timeout)
    Core::DisplayMessage(tr("Step out timed out!").toStdString(), 2000);
  else
    Core::DisplayMessage(tr("Step out successful!").toStdString(), 2000);
}

void CodeWidget::Skip()
{
  m_system.GetPPCState().pc += 4;
  ShowPC();
}

void CodeWidget::ShowPC()
{
  m_code_view->SetAddress(m_system.GetPPCState().pc, CodeViewWidget::SetAddressUpdate::WithUpdate);
  Update();
}

void CodeWidget::SetPC()
{
  m_system.GetPPCState().pc = m_code_view->GetAddress();
  Update();
}

void CodeWidget::ToggleBreakpoint()
{
  m_code_view->ToggleBreakpoint();
}

void CodeWidget::AddBreakpoint()
{
  m_code_view->AddBreakpoint();
}
