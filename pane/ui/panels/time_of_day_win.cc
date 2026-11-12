// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/panels/time_of_day.h"

#include <windows.h>

#include <string>

#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"

namespace pane {

// The system's formatter, in the user's region and the short time Settings
// keeps for it (LOCALE_SSHORTTIME): "17:44" or "5:44 PM".
std::u16string TimeOfDay(base::Time time) {
  std::wstring format(80, L'\0');
  const int format_length =
      GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SSHORTTIME,
                      format.data(), static_cast<int>(format.size()));
  if (format_length <= 1) {
    return std::u16string();
  }
  format.resize(format_length - 1);

  base::Time::Exploded exploded;
  time.LocalExplode(&exploded);
  SYSTEMTIME local = {};
  local.wYear = static_cast<WORD>(exploded.year);
  local.wMonth = static_cast<WORD>(exploded.month);
  local.wDayOfWeek = static_cast<WORD>(exploded.day_of_week);
  local.wDay = static_cast<WORD>(exploded.day_of_month);
  local.wHour = static_cast<WORD>(exploded.hour);
  local.wMinute = static_cast<WORD>(exploded.minute);
  local.wSecond = static_cast<WORD>(exploded.second);

  std::wstring text(64, L'\0');
  const int length =
      GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &local, format.c_str(),
                      text.data(), static_cast<int>(text.size()));
  if (length <= 1) {
    return std::u16string();
  }
  text.resize(length - 1);
  return base::WideToUTF16(text);
}

}  // namespace pane
