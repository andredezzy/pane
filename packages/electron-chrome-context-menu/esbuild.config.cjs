const esbuild = require("esbuild");

const external = ["electron"];

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
];

Promise.all(configs.map((c) => esbuild.build(c)))
	.then(() => console.log("electron-chrome-context-menu built successfully"))
	.catch((e) => {
		console.error(e);
		process.exit(1);
	});
