# A page is loading only while its main frame loads a new document

A tab counts as loading only while its main frame loads a new document, Chrome's `ShouldShowLoadingUI`, and not while any frame in it loads, because an iframe made a tab long loaded show the address bar's progress (the user's report: switching to a tab long loaded showed it for a few seconds). The engine had reported the frame tree's loading (`DidStartLoading` and `DidStopLoading`), which an iframe starts too: an embed or a widget loading as its tab comes forward, an ad refreshing. Chrome's own toolbar and tabs show `ShouldShowLoadingUI`, and so do Dia (its `isLoading` follows that state) and Aside (its bar starts on cross-document navigations of the main frame alone), as Safari and Chrome for Android do too.

`TabBridge` (`engine/pane/browser/ui/tab_bridge.mm`) reports that state at the frame tree's start and stop and at each of the main frame's progress reports. Together they cover every change: a main frame's load reports its progress as it starts and as it ends, each after its state changed. The address bar's progress follows that state (ADR 0082).

`page-loading` adds a 1.5-second iframe to a loaded page: the old engine reported the tab loading for all of it (14 samples of 14), the new one never. A tab coming forward shows as loaded, and a slow navigation still shows as loading from its start to its end. The background scenarios passed, 55 of them; the 8 that bring the app forward waited, the user's Pane being open.

The fix is in the engine, so the engine's key changed, to `0d0a01f8c703` (ADR 0049). Its archive replaced `engine-4c2b8172f926`, which was waiting to be published.
