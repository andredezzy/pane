// Checks that each Chrome UI surface named in Phase 3 (task 3.1) is either
// overridden in //pane or recorded as disabled, by finding the code (or the
// setting) that does it. Prints hooks-check.txt.
// Usage: bun hooks-check.ts [out file]
import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";

const ENGINE = join(import.meta.dir, "..", "..");

type Check = {
	surface: string;
	chrome: string;
	how: "overridden" | "disabled" | "kept";
	file: string;
	needle: string;
};

const CHECKS: Check[] = [
	{
		surface: "JavaScript dialogs (alert, confirm, prompt)",
		chrome:
			"javascript_dialogs::TabModalDialogManager → JavaScriptTabModalDialogViewViews",
		how: "overridden",
		file: "pane/browser/ui/web_contents_delegate.mm",
		needle: "HostedWebContentsDelegate::GetJavaScriptDialogManager",
	},
	{
		surface: "beforeunload dialog",
		chrome: "TabModalDialogManager::RunBeforeUnloadDialog",
		how: "overridden",
		file: "pane/browser/ui/javascript_dialog_manager.mm",
		needle: "JavaScriptDialogManager::RunBeforeUnloadDialog",
	},
	{
		surface: "Form resubmission warning",
		chrome:
			"BrowserWebContentsDelegate::ShowRepostFormWarningDialog (TabModalConfirmDialog)",
		how: "overridden",
		file: "pane/browser/ui/web_contents_delegate.mm",
		needle: "HostedWebContentsDelegate::ShowRepostFormWarningDialog",
	},
	{
		surface: "Permission prompts",
		chrome: "CreatePermissionPrompt (PermissionPromptBubbleView, chips)",
		how: "overridden",
		file: "patches/0004-pane-ui-hooks.patch",
		needle: "g_pane_create_permission_prompt(web_contents, delegate)",
	},
	{
		surface: "HTTP and proxy authentication",
		chrome: "LoginHandler::Create → LoginHandlerViews",
		how: "overridden",
		file: "patches/0004-pane-ui-hooks.patch",
		needle: "g_pane_create_login_handler(auth_info, web_contents",
	},
	{
		surface: "Context menu",
		chrome:
			"RenderViewContextMenuMac (WebContentsViewDelegate::ShowContextMenu)",
		how: "overridden",
		file: "pane/browser/ui/web_contents_delegate.mm",
		needle: "HostedWebContentsDelegate::HandleContextMenu",
	},
	{
		surface: "File choosers (inputs, File System Access, Save As)",
		chrome:
			"ui::SelectFileDialog (SelectFileDialogImpl, NSOpenPanel/NSSavePanel)",
		how: "overridden",
		file: "pane/browser/ui/select_file_dialog.mm",
		needle: "ui::SelectFileDialog::SetFactory(",
	},
	{
		surface: "External protocol dialog",
		chrome:
			"ExternalProtocolHandler::RunExternalProtocolDialog → ExternalProtocolDialog",
		how: "overridden",
		file: "patches/0004-pane-ui-hooks.patch",
		needle: "g_pane_external_protocol_dialog(url, web_contents",
	},
	{
		surface: "Hung renderer dialog",
		chrome:
			"BrowserWebContentsDelegate::RendererUnresponsive → HungRendererDialogView",
		how: "overridden",
		file: "pane/browser/ui/web_contents_delegate.mm",
		needle: "HostedWebContentsDelegate::RendererUnresponsive",
	},
	{
		surface: "Sad tab",
		chrome: "SadTabHelper → SadTabView (in the BrowserView)",
		how: "overridden",
		file: "patches/0003-pane-hosted-browsers.patch",
		needle: "chrome/browser/ui/sad_tab_controller.cc",
	},
	{
		surface: "Certificate interstitials",
		chrome: "SSLBlockingPage (rendered as web content in the tab)",
		how: "kept",
		file: "tests/scenarios/certificate-interstitial.ts",
		needle: "Privacy error",
	},
	{
		surface: "HTML5 fullscreen inside the window, and its Esc bubble",
		chrome: "ExclusiveAccessBubbleViews",
		how: "overridden",
		file: "pane/browser/ui/browser_window.mm",
		needle: "host_->ShowExclusiveAccessHint(hint)",
	},
	{
		surface: "Picture-in-picture",
		chrome:
			"VideoOverlayWindowViews (kept: the player that hosts the video's compositor frame)",
		how: "kept",
		file: "pane/browser/ui/tab_bridge.mm",
		needle: "TabBridge::MediaPictureInPictureChanged",
	},
	{
		surface: "Print",
		chrome:
			"PrintPreviewDialogController (WebUI preview); system NSPrintPanel kept",
		how: "disabled",
		file: "pane/browser/browser_application.mm",
		needle: "--disable-print-preview",
	},
	{
		surface: "Print (delegate callback)",
		chrome: "PrintViewManagerBase::ScriptedPrint",
		how: "overridden",
		file: "patches/0004-pane-ui-hooks.patch",
		needle: "g_pane_will_print(web_contents())",
	},
	{
		surface: "DevTools (docked)",
		chrome: "DevtoolsUIController (BrowserView's docked DevTools)",
		how: "overridden",
		file: "patches/0004-pane-ui-hooks.patch",
		needle: "g_pane_update_devtools(inspected_browser, inspected_web_contents)",
	},
	{
		surface: "DevTools (undocked)",
		chrome: "DevToolsWindow's own Browser and BrowserView window",
		how: "overridden",
		file: "pane/browser/ui/window_host.mm",
		needle: "return CreateDevToolsWindow(browser);",
	},
	{
		surface: "Password manager bubbles",
		chrome: "PasswordBubbleViewBase (Save password?, …)",
		how: "disabled",
		file: "pane/browser/browser_context.mm",
		needle: "password_manager::prefs::kCredentialsEnableService",
	},
	{
		surface: "Autofill bubbles and popups",
		chrome: "Autofill address and card bubbles, suggestion popups",
		how: "disabled",
		file: "pane/browser/browser_context.mm",
		needle: "autofill::prefs::kAutofillProfileEnabled",
	},
	{
		surface: "Device choosers (USB, HID, serial, Bluetooth)",
		chrome: "chrome::ShowDeviceChooserDialog → ChooserBubbleUi",
		how: "overridden",
		file: "patches/0004-pane-ui-hooks.patch",
		needle: "g_pane_device_chooser(owner, controller)",
	},
	{
		surface: "Screen capture picker",
		chrome: "DesktopMediaPickerViews (getDisplayMedia)",
		how: "overridden",
		file: "pane/browser/ui/web_contents_delegate.mm",
		needle: "HostedWebContentsDelegate::RequestMediaAccessPermission",
	},
	{
		surface: "Cast dialog",
		chrome: "MediaRouter's CastDialogView",
		how: "disabled",
		file: "pane/browser/browser_application.mm",
		needle: "Glic,MediaRouter",
	},
	{
		surface: "Gemini in Chrome (Glic) menu bar item",
		chrome: "GlicStatusIcon",
		how: "disabled",
		file: "pane/browser/browser_application.mm",
		needle: "Glic,MediaRouter",
	},
	{
		surface: "Testing field trials (Omnibox Everywhere menu bar item, …)",
		chrome: "testing/variations/fieldtrial_testing_config.json",
		how: "disabled",
		file: "args/release.gn",
		needle: "disable_fieldtrial_testing_config = true",
	},
	{
		surface: "Main menu",
		chrome: "chrome::BuildMainMenu, AppController's menu bridges",
		how: "disabled",
		file: "patches/0002-pane-host-mode.patch",
		needle: "Pane: the hosting app owns the menu bar",
	},
];

const lines: string[] = [
	"# Chrome UI surfaces in Pane's tabs (Phase 3, task 3.1)",
	"# overridden: Pane's code shows it through the app · disabled: off by setting or switch · kept: Chrome's own, by decision (docs/decisions.md)",
	"",
];

let missing = 0;

for (const check of CHECKS) {
	const path = join(ENGINE, check.file);
	let found = false;

	try {
		found = readFileSync(path, "utf8").includes(check.needle);
	} catch {}

	if (!found) {
		missing++;
	}

	lines.push(
		`${found ? "OK     " : "MISSING"} ${check.how.padEnd(10)} ${check.surface}`,
		`        Chrome: ${check.chrome}`,
		`        Pane:   ${check.file} — ${check.needle}`,
		"",
	);
}

lines.push(
	`${CHECKS.length - missing}/${CHECKS.length} surfaces accounted for; missing: ${missing}`,
);

const output = `${lines.join("\n")}\n`;
const target = process.argv[2];

if (target) {
	writeFileSync(target, output);
}

console.log(output);
process.exit(missing === 0 ? 0 : 1);
