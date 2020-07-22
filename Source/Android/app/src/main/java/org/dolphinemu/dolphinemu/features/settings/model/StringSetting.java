package org.dolphinemu.dolphinemu.features.settings.model;

import org.dolphinemu.dolphinemu.NativeLibrary;

public enum StringSetting implements AbstractStringSetting
{
  // These entries have the same names and order as in C++, just for consistency.

  MAIN_DEFAULT_ISO(Settings.FILE_DOLPHIN, Settings.SECTION_INI_CORE, "DefaultISO", ""),
  MAIN_GFX_BACKEND(Settings.FILE_DOLPHIN, Settings.SECTION_INI_CORE, "GFXBackend",
          NativeLibrary.GetDefaultGraphicsBackendName()),

  MAIN_DUMP_PATH(Settings.FILE_DOLPHIN, Settings.SECTION_INI_GENERAL, "DumpPath", ""),
  MAIN_LOAD_PATH(Settings.FILE_DOLPHIN, Settings.SECTION_INI_GENERAL, "LoadPath", ""),
  MAIN_RESOURCEPACK_PATH(Settings.FILE_DOLPHIN, Settings.SECTION_INI_GENERAL, "ResourcePackPath",
          ""),
  MAIN_FS_PATH(Settings.FILE_DOLPHIN, Settings.SECTION_INI_GENERAL, "NANDRootPath", ""),
  MAIN_SD_PATH(Settings.FILE_DOLPHIN, Settings.SECTION_INI_GENERAL, "WiiSDCardPath", ""),

  GFX_ENHANCE_POST_SHADER(Settings.FILE_GFX, Settings.SECTION_GFX_ENHANCEMENTS,
          "PostProcessingShader", "");

  private final String mFile;
  private final String mSection;
  private final String mKey;
  private final String mDefaultValue;

  StringSetting(String file, String section, String key, String defaultValue)
  {
    mFile = file;
    mSection = section;
    mKey = key;
    mDefaultValue = defaultValue;
  }

  @Override
  public boolean delete(Settings settings)
  {
    if (NativeConfig.isSettingSaveable(mFile, mSection, mKey))
    {
      return NativeConfig.deleteKey(settings.getActiveLayer(), mFile, mSection, mKey);
    }
    else
    {
      return settings.getSection(mFile, mSection).delete(mKey);
    }
  }

  @Override
  public String getString(Settings settings)
  {
    if (NativeConfig.isSettingSaveable(mFile, mSection, mKey))
    {
      return NativeConfig.getString(settings.getActiveLayer(), mFile, mSection, mKey,
              mDefaultValue);
    }
    else
    {
      return settings.getSection(mFile, mSection).getString(mKey, mDefaultValue);
    }
  }

  @Override
  public void setString(Settings settings, String newValue)
  {
    if (NativeConfig.isSettingSaveable(mFile, mSection, mKey))
    {
      NativeConfig.setString(settings.getActiveLayer(), mFile, mSection, mKey, newValue);
    }
    else
    {
      settings.getSection(mFile, mSection).setString(mKey, newValue);
    }
  }
}
