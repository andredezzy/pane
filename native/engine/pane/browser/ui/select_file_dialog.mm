// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/ui/select_file_dialog.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/sys_string_conversions.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/ui/window_host.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/shell_dialogs/select_file_dialog_factory.h"
#include "ui/shell_dialogs/select_file_policy.h"
#include "ui/shell_dialogs/selected_file_info.h"

#import "pane/browser/ui/dialogs_internal.h"
#import "pane/public/PaneBrowserWindow.h"

namespace pane {

namespace {

PaneFileChooserMode ModeFor(ui::SelectFileDialog::Type type) {
  switch (type) {
    case ui::SelectFileDialog::SELECT_FOLDER:
    case ui::SelectFileDialog::SELECT_UPLOAD_FOLDER:
    case ui::SelectFileDialog::SELECT_EXISTING_FOLDER:
      return PaneFileChooserModeOpenFolder;
    case ui::SelectFileDialog::SELECT_SAVEAS_FILE:
      return PaneFileChooserModeSave;
    case ui::SelectFileDialog::SELECT_OPEN_MULTI_FILE:
      return PaneFileChooserModeOpenMultiple;
    case ui::SelectFileDialog::SELECT_NONE:
    case ui::SelectFileDialog::SELECT_OPEN_FILE:
      return PaneFileChooserModeOpen;
  }
  return PaneFileChooserModeOpen;
}

const char* ModeName(PaneFileChooserMode mode) {
  switch (mode) {
    case PaneFileChooserModeOpen:
      return "open";
    case PaneFileChooserModeOpenMultiple:
      return "openMultiple";
    case PaneFileChooserModeOpenFolder:
      return "openFolder";
    case PaneFileChooserModeSave:
      return "save";
  }
  return "";
}

class SelectFileDialog : public ui::SelectFileDialog {
 public:
  SelectFileDialog(Listener* listener,
                   std::unique_ptr<ui::SelectFilePolicy> policy)
      : ui::SelectFileDialog(listener, std::move(policy)) {}

  // ui::BaseShellDialog:
  bool IsRunning(gfx::NativeWindow owning_window) const override {
    if (fallback_) {
      return fallback_->IsRunning(owning_window);
    }
    return chooser_ && !chooser_.answered && owning_window == owning_window_;
  }

  void ListenerDestroyed() override {
    listener_ = nullptr;
    if (fallback_) {
      fallback_->ListenerDestroyed();
    }
    [chooser_ withdraw];
  }

 protected:
  // ui::SelectFileDialog:
  void SelectFileImpl(Type type,
                      const std::u16string& title,
                      const base::FilePath& default_path,
                      const FileTypeInfo* file_types,
                      int file_type_index,
                      const base::FilePath::StringType& default_extension,
                      gfx::NativeWindow owning_window,
                      const GURL* caller) override {
    NSWindow* ns_window = owning_window.GetNativeNSWindow();
    WindowHost* host = ns_window ? WindowHost::ForNSWindow(ns_window) : nullptr;
    PaneBrowserWindow* window = host ? host->owner() : nil;
    id<PaneBrowserWindowDelegate> delegate = window.delegate;
    if (![delegate respondsToSelector:@selector(browserWindow:
                                             runFileChooser:)]) {
      // Not one of Pane's windows: Chrome's own panel answers the listener.
      fallback_ = ui::CreateSelectFileDialog(listener_.get(), nullptr);
      fallback_->SelectFile(type, title, default_path, file_types,
                            file_type_index, default_extension, owning_window,
                            caller);
      return;
    }

    type_ = type;
    owning_window_ = owning_window;
    const PaneFileChooserMode mode = ModeFor(type);

    NSMutableArray<NSString*>* extensions = [NSMutableArray array];
    base::ListValue traced;
    if (file_types && !file_types->include_all_files) {
      for (const auto& group : file_types->extensions) {
        for (const auto& extension : group) {
          [extensions addObject:base::SysUTF8ToNSString(extension)];
          traced.Append(extension);
        }
      }
    }

    // A save starts from the suggested file's folder; an open from the
    // folder given, or the given file's folder.
    NSURL* directory = nil;
    NSString* name = @"";
    if (!default_path.empty()) {
      const bool names_file = mode == PaneFileChooserModeSave ||
                              !default_path.Extension().empty();
      const base::FilePath folder =
          names_file ? default_path.DirName() : default_path;
      if (!folder.empty() && folder != base::FilePath(".")) {
        directory = [NSURL
            fileURLWithPath:base::SysUTF8ToNSString(folder.value())
                isDirectory:YES];
      }
      if (mode == PaneFileChooserModeSave) {
        name = base::SysUTF8ToNSString(default_path.BaseName().value());
      }
    }

    chooser_ = [[PaneFileChooser alloc]
        initWithMode:mode
               title:base::SysUTF16ToNSString(title)
          extensions:extensions
           directory:directory
         defaultName:name
              window:window
              answer:base::BindOnce(&SelectFileDialog::OnAnswer,
                                    base::WrapRefCounted(this))];
    Trace("callback", "PaneBrowserWindowDelegate.runFileChooser",
          base::DictValue()
              .Set("mode", ModeName(mode))
              .Set("extensions", std::move(traced)));
    [delegate browserWindow:window runFileChooser:chooser_];
  }

  bool HasMultipleFileTypeChoicesImpl() override { return false; }

 private:
  ~SelectFileDialog() override = default;

  void OnAnswer(std::optional<std::vector<base::FilePath>> paths) {
    if (!listener_) {
      return;
    }
    if (!paths || paths->empty()) {
      listener_->FileSelectionCanceled();
    } else if (type_ == SELECT_OPEN_MULTI_FILE) {
      listener_->MultiFilesSelected(
          ui::FilePathListToSelectedFileInfoList(*paths));
    } else {
      listener_->FileSelected(ui::SelectedFileInfo(paths->front()), 0);
    }
  }

  Type type_ = SELECT_NONE;
  gfx::NativeWindow owning_window_;
  PaneFileChooser* __strong chooser_;
  scoped_refptr<ui::SelectFileDialog> fallback_;
};

class SelectFileDialogFactory : public ui::SelectFileDialogFactory {
 public:
  ui::SelectFileDialog* Create(
      ui::SelectFileDialog::Listener* listener,
      std::unique_ptr<ui::SelectFilePolicy> policy) override {
    return new SelectFileDialog(listener, std::move(policy));
  }
};

}  // namespace

void InstallSelectFileDialogFactory() {
  ui::SelectFileDialog::SetFactory(
      std::make_unique<SelectFileDialogFactory>());
}

}  // namespace pane
