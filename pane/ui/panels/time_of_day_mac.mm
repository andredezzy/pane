// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/panels/time_of_day.h"

#import <Foundation/Foundation.h>

#include "base/strings/sys_string_conversions.h"
#include "base/time/time.h"

namespace pane {

std::u16string TimeOfDay(base::Time time) {
  return base::SysNSStringToUTF16([NSDateFormatter
      localizedStringFromDate:time.ToNSDate()
                    dateStyle:NSDateFormatterNoStyle
                    timeStyle:NSDateFormatterShortStyle]);
}

}  // namespace pane
