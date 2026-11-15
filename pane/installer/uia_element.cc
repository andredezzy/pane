// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/installer/uia_element.h"

#include "base/strings/strcat.h"
#include "base/win/scoped_bstr.h"

#include <uiautomationcoreapi.h>

namespace pane::installer {

namespace {

// `text` as a property's value.
HRESULT Text(const std::wstring& text, VARIANT* value) {
  value->bstrVal = ::SysAllocString(text.c_str());
  if (!value->bstrVal) {
    return E_OUTOFMEMORY;
  }
  value->vt = VT_BSTR;
  return S_OK;
}

void Flag(bool on, VARIANT* value) {
  value->vt = VT_BOOL;
  value->boolVal = on ? VARIANT_TRUE : VARIANT_FALSE;
}

// Says `text` at once, over what was being said of the same screen: Windows
// 10 1709's UiaRaiseNotificationEvent, looked up as Chromium's
// BrowserAccessibilityManagerWin looks it up.
void Say(IRawElementProviderSimple* element, const std::wstring& text) {
  using RaiseNotification =
      HRESULT(WINAPI*)(IRawElementProviderSimple*, NotificationKind,
                       NotificationProcessing, BSTR, BSTR);
  static const auto raise = reinterpret_cast<RaiseNotification>(
      ::GetProcAddress(::GetModuleHandleW(L"uiautomationcore.dll"),
                       "UiaRaiseNotificationEvent"));
  if (!raise) {
    return;
  }
  base::win::ScopedBstr said(text);
  base::win::ScopedBstr activity(L"Pane installer screen");
  raise(element, NotificationKind_Other,
        NotificationProcessing_ImportantMostRecent, said.Get(), activity.Get());
}

}  // namespace

UiaElement::UiaElement(HWND hwnd, Kind kind) : hwnd_(hwnd), kind_(kind) {}

UiaElement::~UiaElement() = default;

void UiaElement::Set(const std::wstring& name,
                     const std::wstring& description) {
  const bool renamed = !name_.empty() && name != name_;
  name_ = name;
  description_ = description;
  if (kind_ == Kind::kDialog && renamed && hwnd_ &&
      ::UiaClientsAreListening()) {
    Say(this, description_.empty()
                  ? name_
                  : base::StrCat({name_, L". ", description_}));
  }
}

void UiaElement::Detach() {
  if (!hwnd_) {
    return;
  }
  // UI Automation lets go of the window's element (WM_GETOBJECT's remarks;
  // Chromium's HWNDMessageHandler::OnDestroy).
  ::UiaReturnRawElementProvider(hwnd_, 0, 0, nullptr);
  hwnd_ = nullptr;
  ::UiaDisconnectProvider(this);
}

LRESULT UiaElement::Answer(WPARAM wparam, LPARAM lparam) {
  return ::UiaReturnRawElementProvider(hwnd_, wparam, lparam, this);
}

IFACEMETHODIMP UiaElement::get_ProviderOptions(ProviderOptions* options) {
  if (!options) {
    return E_INVALIDARG;
  }
  // Called on the window's thread, through COM.
  *options = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider |
                                          ProviderOptions_UseComThreading);
  return S_OK;
}

IFACEMETHODIMP UiaElement::GetPatternProvider(PATTERNID pattern,
                                              IUnknown** provider) {
  if (!provider) {
    return E_INVALIDARG;
  }
  *provider = nullptr;
  if (!hwnd_) {
    return UIA_E_ELEMENTNOTAVAILABLE;
  }
  if (kind_ == Kind::kBox && pattern == UIA_TogglePatternId) {
    return QueryInterface(IID_PPV_ARGS(provider));
  }
  return S_OK;
}

IFACEMETHODIMP UiaElement::GetPropertyValue(PROPERTYID property,
                                            VARIANT* value) {
  if (!value) {
    return E_INVALIDARG;
  }
  // VT_EMPTY leaves the property to the window's own provider.
  ::VariantInit(value);
  if (!hwnd_) {
    return UIA_E_ELEMENTNOTAVAILABLE;
  }
  switch (property) {
    case UIA_NamePropertyId:
      return Text(name_, value);
    case UIA_FullDescriptionPropertyId:
      return description_.empty() ? S_OK : Text(description_, value);
    case UIA_ControlTypePropertyId:
      value->vt = VT_I4;
      value->lVal = kind_ == Kind::kDialog ? UIA_WindowControlTypeId
                                           : UIA_CheckBoxControlTypeId;
      return S_OK;
    case UIA_IsDialogPropertyId:
      if (kind_ == Kind::kDialog) {
        Flag(true, value);
      }
      return S_OK;
    case UIA_IsKeyboardFocusablePropertyId:
      if (kind_ == Kind::kBox) {
        Flag(::IsWindowEnabled(hwnd_), value);
      }
      return S_OK;
    case UIA_HasKeyboardFocusPropertyId:
      if (kind_ == Kind::kBox) {
        Flag(::GetFocus() == hwnd_, value);
      }
      return S_OK;
  }
  return S_OK;
}

IFACEMETHODIMP UiaElement::get_HostRawElementProvider(
    IRawElementProviderSimple** provider) {
  if (!provider) {
    return E_INVALIDARG;
  }
  *provider = nullptr;
  if (!hwnd_) {
    return UIA_E_ELEMENTNOTAVAILABLE;
  }
  // The window's own: where it is, its children, its focus.
  return ::UiaHostProviderFromHwnd(hwnd_, provider);
}

IFACEMETHODIMP UiaElement::Toggle() {
  if (!hwnd_ || kind_ != Kind::kBox) {
    return UIA_E_ELEMENTNOTAVAILABLE;
  }
  if (!::IsWindowEnabled(hwnd_)) {
    return UIA_E_ELEMENTNOTENABLED;
  }
  // As a click on the box: ticked or clear, then BN_CLICKED to the window.
  const bool ticked = ::SendMessageW(hwnd_, BM_GETCHECK, 0, 0) == BST_CHECKED;
  ::SendMessageW(hwnd_, BM_SETCHECK, ticked ? BST_UNCHECKED : BST_CHECKED, 0);
  ::SendMessageW(::GetParent(hwnd_), WM_COMMAND,
                 MAKEWPARAM(::GetDlgCtrlID(hwnd_), BN_CLICKED),
                 reinterpret_cast<LPARAM>(hwnd_));
  return S_OK;
}

IFACEMETHODIMP UiaElement::get_ToggleState(ToggleState* state) {
  if (!state) {
    return E_INVALIDARG;
  }
  if (!hwnd_ || kind_ != Kind::kBox) {
    return UIA_E_ELEMENTNOTAVAILABLE;
  }
  *state = ::SendMessageW(hwnd_, BM_GETCHECK, 0, 0) == BST_CHECKED
               ? ToggleState_On
               : ToggleState_Off;
  return S_OK;
}

}  // namespace pane::installer
