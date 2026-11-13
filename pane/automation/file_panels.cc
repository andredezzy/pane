// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/automation/file_panels.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/scoped_refptr.h"
#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/shell_dialogs/select_file_dialog_factory.h"
#include "ui/shell_dialogs/select_file_policy.h"
#include "ui/shell_dialogs/selected_file_info.h"
#include "url/gurl.h"

namespace pane {

namespace {

class WatchedPanel;

// The panels showing, the latest last.
std::vector<WatchedPanel*>& Showing() {
  static base::NoDestructor<std::vector<WatchedPanel*>> showing;
  return *showing;
}

// One panel Chrome asked for: the platform's stock dialog shows it, and
// answers here, as the channel can.
class WatchedPanel : public ui::SelectFileDialog,
                     public ui::SelectFileDialog::Listener {
 public:
  WatchedPanel(Listener* listener, std::unique_ptr<ui::SelectFilePolicy> policy)
      : ui::SelectFileDialog(listener, std::move(policy)) {}

  // Answers as its own buttons would, and takes the panel down: the stock
  // dialog's going cancels it (SelectFileDialogBridge's destructor), heard
  // by no one.
  void Answer(std::optional<std::vector<base::FilePath>> paths) {
    Close();
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

  base::Value Shown() const {
    return base::Value(
        base::DictValue()
            .Set("kind", "fileChooser")
            .Set("panel", type_ == SELECT_SAVEAS_FILE ? "save" : "open")
            .Set("message", base::UTF16ToUTF8(title_)));
  }

  // ui::BaseShellDialog:
  bool IsRunning(gfx::NativeWindow owning_window) const override {
    return stock_ && stock_->IsRunning(owning_window);
  }

  void ListenerDestroyed() override {
    listener_ = nullptr;
    Close();
  }

  // ui::SelectFileDialog::Listener, the stock dialog's own answer:
  void FileSelected(const ui::SelectedFileInfo& file, int index) override {
    Close();
    if (listener_) {
      listener_->FileSelected(file, index);
    }
  }

  void MultiFilesSelected(
      const std::vector<ui::SelectedFileInfo>& files) override {
    Close();
    if (listener_) {
      listener_->MultiFilesSelected(files);
    }
  }

  void FileSelectionCanceled() override {
    Close();
    if (listener_) {
      listener_->FileSelectionCanceled();
    }
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
    type_ = type;
    title_ = title;
    owner_ = owning_window;
    Showing().push_back(this);
    // The platform's own, as Chrome's when no factory is set, once the stock
    // panels before it have ended.
    WhenStockPanelsEnded(
        owning_window,
        base::BindOnce(&WatchedPanel::ShowStock, base::WrapRefCounted(this),
                       type, title, default_path,
                       file_types ? std::optional(*file_types) : std::nullopt,
                       file_type_index, default_extension,
                       caller ? std::optional(*caller) : std::nullopt));
  }

  bool HasMultipleFileTypeChoicesImpl() override {
    return stock_ && stock_->HasMultipleFileTypeChoices();
  }

 private:
  ~WatchedPanel() override { Close(); }

  // The stock panel shown, unless the panel was answered or let go first.
  void ShowStock(Type type,
                 const std::u16string& title,
                 const base::FilePath& default_path,
                 const std::optional<FileTypeInfo>& file_types,
                 int file_type_index,
                 const base::FilePath::StringType& default_extension,
                 const std::optional<GURL>& caller) {
    if (!std::ranges::contains(Showing(), this)) {
      return;
    }
    stock_ = ui::CreateSelectFileDialog(this, nullptr);
    stock_->SelectFile(type, title, default_path,
                       file_types ? &*file_types : nullptr, file_type_index,
                       default_extension, owner_, caller ? &*caller : nullptr);
  }

  // The stock dialog goes, and with it the panel, answering no one.
  void Close() {
    std::erase(Showing(), this);
    if (scoped_refptr<ui::SelectFileDialog> stock = std::move(stock_)) {
      stock->ListenerDestroyed();
      TakeDownStockPanel(owner_);
    }
  }

  Type type_ = SELECT_NONE;
  std::u16string title_;
  gfx::NativeWindow owner_ = gfx::NativeWindow();
  scoped_refptr<ui::SelectFileDialog> stock_;
};

class WatchedPanels : public ui::SelectFileDialogFactory {
 public:
  ui::SelectFileDialog* Create(
      ui::SelectFileDialog::Listener* listener,
      std::unique_ptr<ui::SelectFilePolicy> policy) override {
    return new WatchedPanel(listener, std::move(policy));
  }
};

}  // namespace

void WatchFilePanels() {
  ui::SelectFileDialog::SetFactory(std::make_unique<WatchedPanels>());
}

base::Value FilePanelShown() {
  return Showing().empty() ? base::Value() : Showing().back()->Shown();
}

bool AnswerFilePanel(std::optional<std::vector<base::FilePath>> paths) {
  if (Showing().empty()) {
    return false;
  }
  // Kept alive while it answers: the page's chooser may let it go.
  scoped_refptr<WatchedPanel> panel(Showing().back());
  panel->Answer(std::move(paths));
  return true;
}

}  // namespace pane
