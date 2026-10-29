# The dmg's volume icon sits on its title's centre line

The dmg's volume icon is Pane's icon with its 16-point art 1.5 points lower, so Finder's title bar shows it on the title's centre line: the user's call, on the installer's window, where the icon sat above "Pane". Finder sets the volume's icon there itself, at 16 points, with its centre 1.5 points above the title's (from its capitals' tops to its baseline): 14.75 points down against 16.25, measured on the user's screenshot and in a Finder window here. Dia's installer sits 1 point off the same way; Pane's icon, a white frame around dark glass, shows it more.

`app/dmg/make-volume-icon.swift` makes `app/dmg/VolumeIcon.icns` from macOS's own drawing of Day (`AppIconDay`, ADR 0089): its 16-point images lowered, the 32-, 128-, and 256-point ones as drawn, for the desktop and Get Info, which show the icon alone. `engine/scripts/dist.sh` gives it to dmgbuild in place of the app's `AppIcon.icns`.

What was rejected:

- **Moving the title:** Finder draws it.
- **Lowering every size:** where the icon stands alone, it is centred as drawn.
- **Another volume icon, a disk:** the installer carries Pane's icon, as Dia's carries Dia's.

How it is checked:

- **Finder:** two test dmgs laid out as the release's, one with the app's icon and one with the new, were mounted and opened. The icon's centre sat 1.50 points above the title's with the app's icon, and 0.00 with the new one.
- **The release's dmg:** a dmg from `dist.sh` carries a `.VolumeIcon.icns` identical to `app/dmg/VolumeIcon.icns`.
- **Finder's sidebar:** it draws a volume as a monochrome disk, so the change shows in the title bar and in 16-point lists alone.
- **Paper:** 08.1 Installer window shows the Day icon, in the title bar and as Pane, in place of the retired v1.

It was measured on macOS 27. A Finder that centres the icon itself would show it 1.5 points low: re-measure, and set `lowered` in the script to 0.
