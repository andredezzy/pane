# VoiceOver names Pane's controls

Pane's own controls read to VoiceOver by what they are and what they hold, not by their placeholders or by the text drawn over them. The profile sheet's fields read their labels (`LabeledField`), not their placeholders. The proxy's fields, the address, find's field, and the Extensions page's search take names. Menu triggers read their option as their value ("Theme, System", "Profile, Alpha"). A profile's row keeps its name as its label and reads its state as its value ("3 tabs, collapsed, proxy connected"), selected when the profile is active. The profile sheet and the PIN screen are modal.

The address field's name sits on the field itself: set on the whole view, it renamed the domain and the title drawn over the field at rest (ADR 0060). The find bar likewise shows no placeholder but keeps one, unseen, for VoiceOver (ADR 0067).

`profile-sheet` reads all of it from the accessibility tree; it failed before.
