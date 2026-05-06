import { injectBrowserAction } from "@pane/electron-chrome-extensions/browser-action";
import { exposeElectronTRPC } from "trpc-electron/main";

injectBrowserAction();
exposeElectronTRPC();
