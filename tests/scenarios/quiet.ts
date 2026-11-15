import { readFileSync } from "node:fs";
import { join } from "node:path";
import { launchPane } from "../lib/pane";
import { expect, type Scenario } from "../run";

// Pane makes no request of its own: a profile, a tab on a local page, and
// half a minute of quiet, recorded in Chrome's net log, reach nothing but
// the local fixtures. Chrome's own background requests (component updates,
// network time, the on-device AI model, AI Mode eligibility, the Translate
// ranker, the Google account list) stay off, as the Electron app never made
// them. Each request a net log holds names its source (traffic annotation).
// Nor does it connect anywhere else: no host is looked up, preconnected, or
// reached but the fixtures' (Chrome's preconnect to its search engine opens
// connections that no request shows). Nor does it detect a proxy, by DHCP or
// by DNS (`wpad`), as Windows' "Automatically detect settings", which it ships
// with, would ask (ADR 0040): Pane follows the system's proxy settings
// without their auto-detection.

type NetLog = {
	constants: { logEventTypes: Record<string, number> };
	events: {
		type: number;
		source: { id: number };
		params?: Record<string, unknown>;
	}[];
};

const quiet: Scenario = async (context) => {
	const netLog = join(context.out, "quiet-netlog.json");

	const app = await launchPane(context, {
		args: [`--log-net-log=${netLog}`, "--net-log-capture-mode=Default"],
	});

	const profile = await app.createProfile("Quiet", "BLUE");
	const url = context.fixtures.url("nav-a.html?quiet");
	await app.openPage(profile, url);
	await Bun.sleep(30_000);
	await app.browser.close();

	const log = JSON.parse(readFileSync(netLog, "utf8")) as NetLog;

	const types = Object.fromEntries(
		Object.entries(log.constants.logEventTypes).map(([name, id]) => [id, name]),
	);

	const requests = new Map<number, { url?: string; annotation?: unknown }>();

	for (const event of log.events) {
		const type = types[event.type];

		if (type !== "URL_REQUEST_START_JOB" && type !== "REQUEST_ALIVE") {
			continue;
		}

		const request = requests.get(event.source.id) ?? {};

		if (typeof event.params?.url === "string") {
			request.url = event.params.url;
		}

		if (event.params?.traffic_annotation !== undefined) {
			request.annotation = event.params.traffic_annotation;
		}

		requests.set(event.source.id, request);
	}

	// Proxy auto-detection, or any PAC script: a quiet session has none.
	const detection = [
		...new Set(
			log.events
				.map((event) => types[event.type])
				.filter(
					(type): type is string =>
						type !== undefined && /^(PAC_FILE_DECIDER|WPAD_)/.test(type),
				),
		),
	];

	expect(
		detection.length === 0,
		`it looked for a proxy of its own: ${detection.join(", ")}`,
	);

	const local = context.fixtures.origin;

	// A log that holds nothing would pass what follows.
	expect(
		[...requests.values()].some((request) => request.url === url),
		`the net log lacks the tab's own request (${requests.size} requests)`,
	);

	const outside = [...requests.values()].filter(
		(request) =>
			request.url &&
			!request.url.startsWith(local) &&
			!/^(data|chrome|chrome-extension|blob):/.test(request.url),
	);

	expect(
		outside.length === 0,
		`requests of its own: ${JSON.stringify(outside)}`,
	);

	// The host a lookup, a preconnect, or a connection names: a URL or an
	// origin ("https://host"), or a host alone, with or without a port.
	const hostOf = (value: string) =>
		value.includes("://") ? new URL(value).hostname : value.split(":")[0];

	const connections = new Set<string>();

	for (const event of log.events) {
		for (const key of ["host", "url"]) {
			const value = event.params?.[key];

			if (typeof value === "string" && value !== "") {
				connections.add(hostOf(value) ?? value);
			}
		}
	}

	const fixtureHost = new URL(local).hostname;

	expect(
		connections.has(fixtureHost),
		`the net log lacks the fixtures' host: ${[...connections].join(", ")}`,
	);

	const elsewhere = [...connections].filter(
		(host) => host !== fixtureHost && host !== "localhost",
	);

	expect(
		elsewhere.length === 0,
		`connections of its own: ${elsewhere.join(", ")}`,
	);
};

export default quiet;
