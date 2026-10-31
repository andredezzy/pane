# An update Pane can't install itself says Quit and install, and its click quits

> Refines [ADR 0092](0092-pane-installs-its-updates.md): what Pane says and does where it can't replace itself.

Where Pane can't install a downloaded update itself, the dmg opens for the user to drag Pane to Applications (ADR 0092). Pane can't when it runs translocated or from a folder its user can't write, when the dmg holds a copy of another identifier or version, or when macOS refuses the swap. Pane said "Downloaded, quit to install", on the pill and on a disabled Settings button, and the pill's click only opened the dmg again. The user, updating 0.13.1 to 0.15.0 across ADR 0114's new identifier (2026-10-31), read it as Pane failing to install by itself, as before, and was not told what to do.

What changes (Paper 06.3.1; 03.3, 03.6, and 06.3 follow it):

- **The pill and the lock screen say Quit and install.** Settings says "Version 0.15.0 is downloaded. It installs by hand.", beside an enabled Quit and install button.
- **The click brings the installer forward and quits Pane** (`Updater.quitAndInstall`). The copy in Applications can only be replaced once it has quit, so the user's one step left is the drag. On the lock screen, the action that fades in under the pointer is Quit, as Restart is for a staged update.

What was rejected:

- **Keeping the click to reopen the dmg**: the user still had to find out that Pane must quit before Finder replaces it.
- **Quitting without the installer**: the dmg the download opened may have been closed by then.
- **Saying why** (another identifier, a folder Pane can't write): the reason is one the user can't act on, and the step is the same.

How it is checked:

- **`update-install`**: offered a dmg holding another version, the pill says Quit and install, and its click quits the copy, which stays as it was and opens nothing.
- **`updates`**: the stand-in dmg, which holds no Pane to stage, says Quit and install, and its click quits Pane without downloading again.
- 0.13.1 and before keep their text: they are published.
