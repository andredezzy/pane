# Spellcheck starts with each profile

The engine brings up each profile's spellchecker as the profile is added, before any of its renderers starts, because `SpellcheckService` hands its dictionaries only to renderers created after it exists, and Chrome creates it through sync, which Pane does not run. The call is `SpellcheckServiceFactory::GetForContext` in `BrowserMainExtraParts::OnProfileAdded` (`engine/pane/browser/browser_main_extra_parts.mm`).

The `context-menu` scenario right-clicks a misspelled word in a text field and expects the spellchecker's suggestions. The word is "recieve": the scenario had used "helo", which macOS accepts, so no word was flagged on any screen, locked or not, and stock Chrome for Testing flagged none either. It waits a fixed time for the spellchecker's verdict before the menu, since no signal shows it.
