// Prints whether macOS's Caps Lock is on ("on" or "off"): while it is, macOS
// floats its Caps Lock indicator (a TUINSWindow) by a focused text field,
// which the scenarios' window checks count as a window the app opened.
import CoreGraphics

print(CGEventSource.flagsState(.combinedSessionState).contains(.maskAlphaShift) ? "on" : "off")
