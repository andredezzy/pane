// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_INSTALLER_WINDOW_H_
#define PANE_INSTALLER_WINDOW_H_

#include <windows.h>

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "pane/installer/screen.h"

namespace pane::installer {

// Pane's install window, and its uninstall dialog (Paper 11.12.6 to
// 11.12.10): Windows 11's frame with no title band, Pane's card, its own
// Minimize and Close, the App icon, and a Screen, drawn with Direct2D and
// DirectWrite in Pane's look, following Windows' light or dark and its
// contrast themes. Its buttons and its box are Windows' own controls, drawn
// as Pane draws its own, so the keys and screen readers find them as any
// dialog's. Its own small process's, not Chrome's: the installer runs before
// Pane's files are there.
class InstallWindow {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;
    virtual void OnButton(int id) = 0;
    virtual void OnBoxToggled(bool ticked) = 0;
    // Close, Alt+F4, or the corner's Close: true closes the window now.
    virtual bool OnCloseAsked() = 0;
    // At each frame a bar shows: how full it is, 0 to 1, and its right
    // label, which an empty one leaves as it is.
    virtual double BarAt(std::u16string& right) = 0;
  };

  explicit InstallWindow(Delegate* delegate);
  InstallWindow(const InstallWindow&) = delete;
  InstallWindow& operator=(const InstallWindow&) = delete;
  ~InstallWindow();

  // Shows `screen`, the window made and shown at once the first time.
  void Show(const Screen& screen);

  // The box's size beside its label, once counted.
  void SetBoxSize(const std::u16string& size);

  // Closes the window; Run returns.
  void Close();

  // Runs `task` on the window's thread, from any thread.
  void Post(base::OnceClosure task);

  // Its messages until the window is gone.
  void Run();

  HWND hwnd() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace pane::installer

#endif  // PANE_INSTALLER_WINDOW_H_
