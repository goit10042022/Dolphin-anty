// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QWidget>

class ConfigBool;
class ConfigChoice;
class ConfigSlider;
class GraphicsWindow;
class QCheckBox;
class QComboBox;
class QPushButton;
class QSlider;
class ToolTipComboBox;
class ToolTipPushButton;

class EnhancementsWidget final : public QWidget
{
  Q_OBJECT
public:
  explicit EnhancementsWidget(const GraphicsWindow* parent);

private:
  void LoadSettings();
  void SaveSettings();

  void CreateWidgets();
  void ConnectWidgets();
  void AddDescriptions() const;
  void ConfigureColorCorrection();
  void ConfigurePostProcessingShader();
  void LoadPPShaders() const;

  // Enhancements
  ConfigChoice* m_ir_combo;
  ToolTipComboBox* m_aa_combo;
  ToolTipComboBox* m_texture_filtering_combo;
  ToolTipComboBox* m_output_resampling_combo;
  ToolTipComboBox* m_pp_effect;
  ToolTipPushButton* m_configure_color_correction;
  QPushButton* m_configure_pp_effect;
  ConfigBool* m_scaled_efb_copy;
  ConfigBool* m_per_pixel_lighting;
  ConfigBool* m_widescreen_hack;
  ConfigBool* m_disable_fog;
  ConfigBool* m_force_24bit_color;
  ConfigBool* m_disable_copy_filter;
  ConfigBool* m_arbitrary_mipmap_detection;
  ConfigBool* m_hdr;

  // Stereoscopy
  ConfigChoice* m_3d_mode;
  ConfigSlider* m_3d_depth;
  ConfigSlider* m_3d_convergence;
  ConfigBool* m_3d_swap_eyes;

  int m_msaa_modes;
  bool m_block_save;
};
