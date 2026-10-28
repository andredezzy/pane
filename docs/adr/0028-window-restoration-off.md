# AppKit's window restoration is off

AppKit's window restoration is off (`ApplePersistenceIgnoreState`, in the defaults the app registers in `app/Pane/main.swift`), because Pane restores its own session (ADR 0016), and after a crash AppKit's "reopen windows?" alert blocked the app. Pane brings its profiles and tabs back from its own `profiles.json`, as the Electron app did; the `restore` scenario checks that they survive quitting and relaunching, in order.
