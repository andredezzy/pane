// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_INSTALLER_UIA_ELEMENT_H_
#define PANE_INSTALLER_UIA_ELEMENT_H_

#include <windows.h>

#include <wrl/implements.h>

#include <string>

#include <uiautomation.h>

namespace pane::installer {

// What a screen reader reads of the install window (Paper 11.12.8, 11.12.10):
// UI Automation's element for one of its windows, which Narrator, NVDA, and
// JAWS read once a window has a provider of its own. The window reads as a
// dialog named by the screen's title, "dialog" as Pane's own dialogs say it
// (Windows says "window" of a window), its message the description, said a
// moment after a screen reader first reaches the window and at each new
// screen; the box as a check box named by its label, its size and line the
// description. The window's own text stays "Pane Installer", what the
// taskbar and Alt+Tab show; the buttons and the bar are Windows' own
// controls, which UI Automation reads as it reads any.
class UiaElement
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
          IRawElementProviderSimple,
          IToggleProvider> {
 public:
  enum class Kind { kDialog, kBox };

  UiaElement(HWND hwnd, Kind kind);
  UiaElement(const UiaElement&) = delete;
  UiaElement& operator=(const UiaElement&) = delete;

  // What it reads as. A dialog given a new name says it at once, with its
  // description, as its new screen shows.
  void Set(const std::wstring& name, const std::wstring& description);

  // Says a dialog's description, after what is being said: Narrator says a
  // dialog's name and kind as it takes the focus, and never its full
  // description, so the uninstall dialog's message went unsaid (Narrator,
  // 2026-11-16: "Uninstall Pane? window", three times, and nothing of "Pane
  // leaves this PC"). The window says it once a screen reader has reached it.
  void SayDescription();

  // Its window is going: nothing is read of it from now on.
  void Detach();

  // Answers WM_GETOBJECT for its window.
  LRESULT Answer(WPARAM wparam, LPARAM lparam);

  // IRawElementProviderSimple:
  IFACEMETHODIMP get_ProviderOptions(ProviderOptions* options) override;
  IFACEMETHODIMP GetPatternProvider(PATTERNID pattern,
                                    IUnknown** provider) override;
  IFACEMETHODIMP GetPropertyValue(PROPERTYID property, VARIANT* value) override;
  IFACEMETHODIMP get_HostRawElementProvider(
      IRawElementProviderSimple** provider) override;

  // IToggleProvider, the box's:
  IFACEMETHODIMP Toggle() override;
  IFACEMETHODIMP get_ToggleState(ToggleState* state) override;

 protected:
  ~UiaElement() override;

 private:
  HWND hwnd_;
  const Kind kind_;
  std::wstring name_;
  std::wstring description_;
};

}  // namespace pane::installer

#endif  // PANE_INSTALLER_UIA_ELEMENT_H_
