// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <QApplication>
#include <QDesktopWidget>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPalette>
#include <QScreen>
#include <QTimer>

#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "DolphinQt2/Host.h"
#include "DolphinQt2/RenderWidget.h"
#include "DolphinQt2/Settings.h"

RenderWidget::RenderWidget(QWidget* parent) : QWidget(parent)
{
  QPalette p;
  p.setColor(QPalette::Background, Qt::black);
  setPalette(p);

  connect(Host::GetInstance(), &Host::RequestTitle, this, &RenderWidget::setWindowTitle);
  connect(Host::GetInstance(), &Host::RequestRenderSize, this, [this](int w, int h) {
    if (!SConfig::GetInstance().bRenderWindowAutoSize || isFullScreen() || isMaximized())
      return;

    resize(w, h);
  });

  connect(&Settings::Instance(), &Settings::EmulationStateChanged, this, [this](Core::State state) {
    SetFillBackground(SConfig::GetInstance().bRenderToMain && state == Core::State::Uninitialized);
  });

  // We have to use Qt::DirectConnection here because we don't want those signals to get queued
  // (which results in them not getting called)
  connect(this, &RenderWidget::StateChanged, Host::GetInstance(), &Host::SetRenderFullscreen,
          Qt::DirectConnection);
  connect(this, &RenderWidget::HandleChanged, Host::GetInstance(), &Host::SetRenderHandle,
          Qt::DirectConnection);
  connect(this, &RenderWidget::SizeChanged, Host::GetInstance(), &Host::ResizeSurface,
          Qt::DirectConnection);
  connect(this, &RenderWidget::PositionChanged, Host::GetInstance(), &Host::MoveSurface,
          Qt::DirectConnection);

  emit HandleChanged((void*)winId());

  m_mouse_timer = new QTimer(this);
  connect(m_mouse_timer, &QTimer::timeout, this, &RenderWidget::HandleCursorTimer);
  m_mouse_timer->setSingleShot(true);
  setMouseTracking(true);

  connect(&Settings::Instance(), &Settings::HideCursorChanged, this,
          &RenderWidget::OnHideCursorChanged);
  OnHideCursorChanged();
  connect(&Settings::Instance(), &Settings::KeepWindowOnTopChanged, this,
          &RenderWidget::OnKeepOnTopChanged);
  OnKeepOnTopChanged(Settings::Instance().IsKeepWindowOnTopEnabled());
  m_mouse_timer->start(MOUSE_HIDE_DELAY);

  SetFillBackground(true);
}

void RenderWidget::SetFillBackground(bool fill)
{
  setAttribute(Qt::WA_OpaquePaintEvent, !fill);
  setAttribute(Qt::WA_NoSystemBackground, !fill);
  setAutoFillBackground(fill);
}

void RenderWidget::OnHideCursorChanged()
{
  setCursor(Settings::Instance().GetHideCursor() ? Qt::BlankCursor : Qt::ArrowCursor);
}

void RenderWidget::OnKeepOnTopChanged(bool top)
{
  const bool was_visible = isVisible();

  setWindowFlags(top ? windowFlags() | Qt::WindowStaysOnTopHint :
                       windowFlags() & ~Qt::WindowStaysOnTopHint);

  if (was_visible)
    show();
}

void RenderWidget::HandleCursorTimer()
{
  if (isActiveWindow())
    setCursor(Qt::BlankCursor);
}

void RenderWidget::showFullScreen()
{
  QWidget::showFullScreen();

  const auto dpr =
      QGuiApplication::screens()[QApplication::desktop()->screenNumber(this)]->devicePixelRatio();

  emit SizeChanged(width() * dpr, height() * dpr);
}

bool RenderWidget::event(QEvent* event)
{
  switch (event->type())
  {
  case QEvent::Paint:
    return !autoFillBackground();
  case QEvent::KeyPress:
  {
    QKeyEvent* ke = static_cast<QKeyEvent*>(event);
    if (ke->key() == Qt::Key_Escape)
      emit EscapePressed();

    if (ke->key() == Qt::Key_Alt)
    {
      releaseMouse();
      m_grabbed = false;
    }

    // The render window might flicker on some platforms because Qt tries to change focus to a new
    // element when there is none (?) Handling this event before it reaches QWidget fixes the issue.
    if (ke->key() == Qt::Key_Tab)
      return true;

    break;
  }
  case QEvent::MouseButtonPress:
    if (SConfig::GetInstance().bGrabCursor)
    {
      grabMouse();
      m_grabbed = true;
    }
  case QEvent::MouseMove:
    if (!Settings::Instance().GetHideCursor() && isActiveWindow())
    {
      setCursor(Qt::ArrowCursor);
      m_mouse_timer->start(MOUSE_HIDE_DELAY);
    }
    if (SConfig::GetInstance().bGrabCursor && m_grabbed)
    {
      const auto win = geometry();
      int new_x = std::min(std::max(QCursor::pos().x(), win.x()), win.x() + win.width());
      int new_y = std::min(std::max(QCursor::pos().y(), win.y()), win.y() + win.height());
      QCursor::setPos(new_x, new_y);
    }
    break;
  case QEvent::WinIdChange:
    emit HandleChanged((void*)winId());
    break;
  case QEvent::WindowActivate:
    Host::GetInstance()->SetRenderFocus(true);
    if (SConfig::GetInstance().m_PauseOnFocusLost && Core::GetState() == Core::State::Paused)
      Core::SetState(Core::State::Running);
    break;
  case QEvent::WindowDeactivate:
    Host::GetInstance()->SetRenderFocus(false);
    if (SConfig::GetInstance().m_PauseOnFocusLost && Core::GetState() == Core::State::Running)
      Core::SetState(Core::State::Paused);
    break;
  case QEvent::Move:
    emit PositionChanged(x(), y());
    break;
  case QEvent::Resize:
  {
    const QResizeEvent* se = static_cast<QResizeEvent*>(event);
    QSize new_size = se->size();

    const auto dpr =
        QGuiApplication::screens()[QApplication::desktop()->screenNumber(this)]->devicePixelRatio();

    emit SizeChanged(new_size.width() * dpr, new_size.height() * dpr);
    break;
  }
  case QEvent::WindowStateChange:
    emit StateChanged(isFullScreen());
    break;
  case QEvent::Close:
    emit Closed();
    break;
  default:
    break;
  }
  return QWidget::event(event);
}
