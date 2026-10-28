# Tracing costs one branch when off

The engine's trace builds a line's arguments only while it traces, so an engine that is not tracing pays one cached branch per call site and nothing more. The trace (`engine/pane/browser/engine_trace.h`) records the traffic between the app and the engine, one JSON line per embedding-API call and per delegate callback, in the file `PANE_ENGINE_TRACE` names; scenarios launch the app with it and assert against the file (`tests/lib/traced.ts`). Its call sites built their arguments first, converting URLs and titles and looking up the tab's bridge, on every load progress tick, traced or not: P1 of the engine review of 2026-10-28.

`PANE_TRACE` and `PANE_TRACE_TAB` test a cached `IsTracing()` before evaluating their arguments, and a call site that builds a line itself tests it first too. A line the trace file cannot take (a full disk) is dropped, where it CHECK-crashed the browser: a test aid never takes the browser down.
