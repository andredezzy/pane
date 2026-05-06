import path from "node:path";
import {
	app,
	BaseWindow,
	BrowserWindow,
	Menu,
	nativeImage,
	WebContentsView,
} from "electron";

export interface AppWindow {
	mainWindow: BaseWindow;
	chrome: WebContentsView;
	surface: BrowserWindow;
}

export function createAppWindow(): AppWindow {
	const resourcesDir = path.join(app.getAppPath(), "resources");
	const icon = nativeImage.createFromPath(path.join(resourcesDir, "icon.png"));

	if (process.platform === "darwin") {
		const dockIcon = nativeImage.createFromPath(
			path.join(resourcesDir, "icon-dock.png"),
		);

		if (!dockIcon.isEmpty()) {
			app.dock?.setIcon(dockIcon);
		}
	}

	const mainWindow = new BaseWindow({
		width: 1280,
		height: 800,
		minWidth: 900,
		minHeight: 600,
		title: "Pane",
		titleBarStyle: "hidden",
		trafficLightPosition: { x: 12, y: 12 },
		transparent: true,
		hasShadow: true,
		show: false,
		icon,
	});

	const webPreferences: Electron.WebPreferences = {
		preload: path.join(__dirname, "../preload/index.js"),
		contextIsolation: true,
		sandbox: true,
	};

	const loadRenderer = (
		wc: Electron.WebContents,
		params?: Record<string, string>,
	) => {
		if (process.env.ELECTRON_RENDERER_URL) {
			const url = new URL(process.env.ELECTRON_RENDERER_URL);

			if (params) {
				for (const [k, v] of Object.entries(params)) {
					url.searchParams.set(k, v);
				}
			}

			wc.loadURL(url.toString());
		} else {
			wc.loadFile(path.join(__dirname, "../renderer/index.html"), {
				query: params,
			});
		}
	};

	const chrome = new WebContentsView({ webPreferences });

	chrome.setBackgroundColor("#0a0a0c");
	mainWindow.contentView.addChildView(chrome);

	const [width, height] = mainWindow.getContentSize();
	chrome.setBounds({ x: 0, y: 0, width, height });

	loadRenderer(chrome.webContents);

	chrome.webContents.once("did-finish-load", () => {
		mainWindow.show();
		chrome.webContents.focus();
	});

	const bounds = mainWindow.getBounds();

	const surface = new BrowserWindow({
		parent: mainWindow,
		frame: false,
		transparent: true,
		hasShadow: false,
		show: false,
		x: bounds.x,
		y: bounds.y,
		width,
		height,
		backgroundColor: "#00000000",
		webPreferences,
	});

	loadRenderer(surface.webContents, { surface: "true" });

	return { mainWindow, chrome, surface };
}

export function createMenu(
	chrome: WebContentsView,
	getActiveTabContents?: () => Electron.WebContents | undefined,
): void {
	const menu = Menu.buildFromTemplate([
		{ role: "appMenu" },
		{ role: "fileMenu" },
		{ role: "editMenu" },
		{
			label: "View",
			submenu: [
				{
					label: "Toggle developer tools",
					accelerator: "CommandOrControl+Option+I",
					click: () => chrome.webContents.openDevTools({ mode: "detach" }),
				},
				{
					label: "Toggle page developer tools",
					accelerator: "CommandOrControl+Shift+I",
					click: () =>
						getActiveTabContents?.()?.openDevTools({ mode: "detach" }),
				},
				{ role: "reload", click: () => chrome.webContents.reload() },
				{ type: "separator" },
				{ role: "resetZoom" },
				{ role: "zoomIn" },
				{ role: "zoomOut" },
				{ type: "separator" },
				{ role: "togglefullscreen" },
			],
		},
		{ role: "windowMenu" },
	]);

	Menu.setApplicationMenu(menu);
}
