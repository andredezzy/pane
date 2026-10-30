import { existsSync, lstatSync, readdirSync, realpathSync } from "node:fs";
import { join } from "node:path";
import {
	type BenchmarkedApp,
	benchmarkedApps,
	conditions,
	launchApp,
	machine,
} from "../lib/benchmark";
import type { Scenario } from "../run";

// The Electron app against the native one, before either runs: each bundle's
// size, on disk and in bytes, with its frameworks and main binaries; then
// one launch each for its engine, Chromium's version and the user agent.

// Bytes on disk (allocated blocks, as du counts them).
async function diskBytes(path: string): Promise<number> {
	const kilobytes = (await Bun.$`du -sk ${path}`.text()).split("\t")[0];

	return Number(kilobytes) * 1024;
}

// The files' own sizes, links not followed.
function apparentBytes(path: string): number {
	const stat = lstatSync(path);

	if (!stat.isDirectory()) {
		return stat.isFile() ? stat.size : 0;
	}

	return readdirSync(path).reduce(
		(total, name) => total + apparentBytes(join(path, name)),
		0,
	);
}

async function sizes(path: string) {
	return {
		diskBytes: await diskBytes(path),
		apparentBytes: apparentBytes(path),
	};
}

async function bundleSizes(app: BenchmarkedApp) {
	const contents = join(app.bundle, "Contents");
	const frameworks = join(contents, "Frameworks");
	const parts: Record<string, Awaited<ReturnType<typeof sizes>>> = {};

	for (const name of readdirSync(frameworks)) {
		parts[`Frameworks/${name}`] = await sizes(join(frameworks, name));

		// A framework's binary, through Versions/Current.
		if (name.endsWith(".framework")) {
			const binary = join(frameworks, name, name.replace(/\.framework$/, ""));

			if (existsSync(binary)) {
				parts[`Frameworks/${name} (binary)`] = await sizes(
					realpathSync(binary),
				);
			}
		}
	}

	parts["MacOS (main executable)"] = await sizes(app.executable);
	parts.Resources = await sizes(join(contents, "Resources"));

	const asar = join(contents, "Resources", "app.asar");

	if (existsSync(asar)) {
		parts["Resources/app.asar"] = await sizes(asar);
	}

	return { bundle: await sizes(app.bundle), parts };
}

const benchmarkBundle: Scenario = async (context) => {
	const started = conditions();
	const apps = await benchmarkedApps(context);
	const results: Record<string, unknown> = {};

	for (const app of apps) {
		const running = await launchApp(context, app, {
			url: context.fixtures.url("smoke.html?engine"),
		});

		const version =
			await running.cdp.send<Record<string, string>>("Browser.getVersion");

		const userAgent = await running.page.evaluate<string>(
			"navigator.userAgent",
		);

		const fullVersionList = await running.page.evaluate<unknown>(
			`navigator.userAgentData?.getHighEntropyValues(["fullVersionList"]).then((values) => values.fullVersionList) ?? null`,
		);

		const viewport = await running.page.evaluate<number[]>(
			"[innerWidth, innerHeight]",
		);

		await running.quit();

		const electronPlist = join(
			app.bundle,
			"Contents/Frameworks/Electron Framework.framework/Resources/Info.plist",
		);

		const electron = existsSync(electronPlist)
			? (
					await Bun.$`plutil -extract CFBundleVersion raw -o - ${electronPlist}`.text()
				).trim()
			: null;

		results[app.kind] = {
			version: app.version,
			sizes: await bundleSizes(app),
			engine: {
				product: version.product,
				protocolVersion: version.protocolVersion,
				jsVersion: version.jsVersion,
				electron,
				userAgent,
				fullVersionList,
			},
			viewport,
		};
	}

	return {
		machine: await machine(),
		conditions: { started, ended: conditions() },
		...results,
	};
};

export default benchmarkBundle;
