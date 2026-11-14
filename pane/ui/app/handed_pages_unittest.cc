// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/handed_pages.h"

#include <vector>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "net/base/filename_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace pane {

namespace {

base::CommandLine LaunchWith(const std::vector<base::FilePath>& args) {
  base::CommandLine command_line(base::FilePath(FILE_PATH_LITERAL("Pane")));
  // As a launch's own: a switch Pane adds, then what the system handed.
  command_line.AppendSwitch("no-startup-window");
  for (const base::FilePath& arg : args) {
    command_line.AppendArgPath(arg);
  }
  return command_line;
}

}  // namespace

// Web pages and files open, in order; what isn't one never reaches the
// engine: another scheme, search terms, an app's own.
TEST(HandedPagesTest, WebPagesAndFilesAlone) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath page = directory.GetPath().AppendASCII("page.html");
  ASSERT_TRUE(base::WriteFile(page, "<p>page</p>"));
  // As the fixer names a file: its path resolved (/var is /private/var on
  // the Mac).
  const GURL file = net::FilePathToFileURL(base::MakeAbsoluteFilePath(page));

  const std::vector<GURL> pages = HandedPages(
      LaunchWith({
          base::FilePath(FILE_PATH_LITERAL("https://example.com/a?b#c")),
          base::FilePath(FILE_PATH_LITERAL("javascript:alert(1)")),
          base::FilePath(FILE_PATH_LITERAL("chrome://settings")),
          base::FilePath(FILE_PATH_LITERAL("? pane")),
          base::FilePath(FILE_PATH_LITERAL("http://127.0.0.1:8080/")),
          page,
          base::FilePath(FILE_PATH_LITERAL("page.html")),
          base::FilePath(FILE_PATH_LITERAL("mailto:someone@example.com")),
      }),
      directory.GetPath());

  EXPECT_EQ(pages, (std::vector<GURL>{
                       GURL("https://example.com/a?b#c"),
                       GURL("http://127.0.0.1:8080/"),
                       file,
                       file,
                   }));
}

// A launch that names nothing hands over no page: the running Pane shows its
// window instead.
TEST(HandedPagesTest, NoneForALaunchAlone) {
  EXPECT_TRUE(HandedPages(LaunchWith({}), base::FilePath()).empty());
}

TEST(HandedPagesTest, IsHandedPage) {
  EXPECT_TRUE(IsHandedPage(GURL("http://example.com/")));
  EXPECT_TRUE(IsHandedPage(GURL("https://example.com/")));
  EXPECT_TRUE(IsHandedPage(GURL("file:///tmp/page.html")));
  EXPECT_FALSE(IsHandedPage(GURL("chrome://settings")));
  EXPECT_FALSE(IsHandedPage(GURL("data:text/html,hi")));
  EXPECT_FALSE(IsHandedPage(GURL()));
}

}  // namespace pane
