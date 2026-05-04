const esbuild = require("esbuild");
const fs = require("node:fs");
const path = require("node:path");

const external = [
	"electron",
	"electron-chrome-extensions/preload",
	"electron-chrome-web-store",
];

const configs = [
	{
		entryPoints: ["src/index.ts"],
		outfile: "dist/cjs/index.js",
		bundle: true,
		platform: "node",
		format: "cjs",
		external,
	},
	{
		entryPoints: ["src/index.ts"],
		outfile: "dist/esm/index.mjs",
		bundle: true,
		platform: "node",
		format: "esm",
		external,
	},
	{
		entryPoints: ["src/preload.ts"],
		outfile: "dist/chrome-extension-api.preload.js",
		bundle: true,
		platform: "browser",
		format: "iife",
		external,
		sourcemap: false,
	},
	{
		entryPoints: ["src/browser-action.ts"],
		outfile: "dist/cjs/browser-action.js",
		bundle: true,
		platform: "browser",
		format: "cjs",
		external,
		sourcemap: false,
	},
	{
		entryPoints: ["src/browser-action.ts"],
		outfile: "dist/esm/browser-action.mjs",
		bundle: true,
		platform: "browser",
		format: "esm",
		external,
		sourcemap: false,
	},
];

Promise.all(configs.map((c) => esbuild.build(c)))
	.then(() => {
		const preloadsDir = path.join(__dirname, "dist", "preloads");
		fs.mkdirSync(preloadsDir, { recursive: true });
		fs.copyFileSync(
			path.join(__dirname, "src", "preloads", "sw.js"),
			path.join(preloadsDir, "sw.js"),
		);
		fs.copyFileSync(
			path.join(__dirname, "src", "preloads", "frame.js"),
			path.join(preloadsDir, "frame.js"),
		);
		console.log("electron-chrome-extensions built successfully");
	})
	.catch((e) => {
		console.error(e);
		process.exit(1);
	});
