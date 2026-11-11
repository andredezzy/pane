// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/panels/time_of_day.h"

#include <CoreFoundation/CoreFoundation.h>

#include "base/apple/scoped_cftyperef.h"
#include "base/strings/sys_string_conversions.h"
#include "base/time/time.h"

namespace pane {

// The system's formatter, the one Foundation's own wraps: the user's region,
// and its 12 or 24 hours as Settings keeps them.
std::u16string TimeOfDay(base::Time time) {
  const base::apple::ScopedCFTypeRef<CFLocaleRef> locale(CFLocaleCopyCurrent());
  const base::apple::ScopedCFTypeRef<CFDateFormatterRef> formatter(
      CFDateFormatterCreate(kCFAllocatorDefault, locale.get(),
                            kCFDateFormatterNoStyle,
                            kCFDateFormatterShortStyle));
  if (!formatter) {
    return std::u16string();
  }
  const base::apple::ScopedCFTypeRef<CFStringRef> text(
      CFDateFormatterCreateStringWithAbsoluteTime(
          kCFAllocatorDefault, formatter.get(), time.ToCFAbsoluteTime()));
  return text ? base::SysCFStringRefToUTF16(text.get()) : std::u16string();
}

}  // namespace pane
