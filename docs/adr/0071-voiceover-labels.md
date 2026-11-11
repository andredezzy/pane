# VoiceOver names Pane's controls

> **On the Views line** (`docs/plans/views-migration.md`, H2): a profile's row keeps its name as its accessible name and "3 tabs, collapsed, proxy connected" as its value, selected while its page shows; a tab's row reads its title (Loading without one), selected while shown; the sidebar's edge is a splitter valued at the width, stepping by 10 (`pane/ui/sidebar/`).

> **On the Views line** (`docs/plans/views-migration.md`, H4): the address keeps its name on the field, and the domain and title over it read as text of their own; Reload reads Stop while the page loads; the update pill's button reads its status, and its dismiss button Dismiss.

> **On the Views line** (`docs/plans/views-migration.md`, What Chrome asks for): a page's dialog reads as a modal dialog by its title, its fields by their placeholders (Text without one, the password protected), and its buttons by their titles, the default marked (`dialogAccessibility` reads them).

> **On the Views line** (`docs/plans/views-migration.md`, What Chrome asks for, second half): find's field reads "Find in page" with no placeholder, its count its text, and its buttons Previous match, Next match, and Close find bar (`findAccessibility` reads them); docked DevTools read "Developer tools".

> **On the Views line** (`docs/plans/views-migration.md`, X1): the puzzle reads Extensions, selected while its menu shows; a pinned action its action's title, else its name, selected while its popup shows; the menu reads as the group Extensions, Installed extensions as a heading, each row by its title, a pin Pin to toolbar or Unpin from toolbar, selected while pinned, and More; the more menu as a group named by its extension (`extensionsAccessibility` reads them).

> **On the Views line** (`docs/plans/views-migration.md`, X3): an extension's prompt reads as a modal dialog by its title, its accept marked the default, and Report abuse as a checkbox, checked or not; Chrome's extensions pages read as Chrome's.

> **On the Views line** (`docs/plans/views-migration.md`, N1): the profile sheet's controls keep the Swift's names and identifiers (`pane.profile.*`, `pane/ui/view_ids.cc`) in its focus order, each switch named by its title with its sentence as its description, the tiles by their platform, selected when chosen; a problem is its field's description, announced when Save is refused.

Pane's own controls read to VoiceOver by what they are and what they hold, not by their placeholders or by the text drawn over them. The profile sheet's fields read their labels (`LabeledField`), not their placeholders. The proxy's fields, the address, find's field, and the Extensions page's search take names. Menu triggers read their option as their value ("Theme, System", "Profile, Alpha"). A profile's row keeps its name as its label and reads its state as its value ("3 tabs, collapsed, proxy connected"), selected when the profile is active. The profile sheet and the PIN screen are modal.

The address field's name sits on the field itself: set on the whole view, it renamed the domain and the title drawn over the field at rest (ADR 0060). The find bar likewise shows no placeholder but keeps one, unseen, for VoiceOver (ADR 0067).

`profile-sheet` reads the profile row's value, the sheet's field names, and its modality from the accessibility tree; it failed before. No scenario reads the other names and values.
