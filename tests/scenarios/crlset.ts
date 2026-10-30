import { existsSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { $ } from "bun";
import { tabOf } from "../lib/pane";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

type NetLog = {
	constants: { logEventTypes: Record<string, number> };
	events: { type: number; params?: Record<string, unknown> }[];
};

// The CRLSet a packaged app carries is the one its certificate verifier
// uses (ADR 0111): the bundle's crl-set is Chrome's, the engine hands it to
// the verifier, which applies it once startup is over (Chrome's: the first
// page painted), and from then on verifying a page's certificate (a
// self-signed one: the verifier runs either way) names that CRLSet's
// sequence in the net log, not the empty built-in one's 0.
const crlset: Scenario = async (context) => {
	const file = join(dirname(dirname(context.binary)), "Resources", "crl-set");

	expect(existsSync(file), `the bundle carries no CRLSet at ${file}`);

	// A 2-byte length, then the header's JSON.
	const bytes = readFileSync(file);

	const header = JSON.parse(
		bytes.subarray(2, 2 + bytes.readUInt16LE(0)).toString("utf8"),
	) as { ContentType: string; Sequence: number };

	expect(
		header.ContentType === "CRLSet" && header.Sequence > 0,
		`the bundle's crl-set is ${JSON.stringify(header)}`,
	);

	const folder = context.tempDir("pane-crlset-");
	await $`openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=127.0.0.1 -keyout ${join(folder, "key.pem")} -out ${join(folder, "cert.pem")}`.quiet();

	const server = Bun.serve({
		hostname: "127.0.0.1",
		port: 0,
		tls: {
			cert: readFileSync(join(folder, "cert.pem"), "utf8"),
			key: readFileSync(join(folder, "key.pem"), "utf8"),
		},
		fetch: () => new Response("<!doctype html><title>CRLSet fixture</title>"),
	});

	const netLog = join(context.out, "crlset-netlog.json");
	let appliedAfter = 0;

	try {
		const { app, trace, waitForEntry } = await launchTraced(context, "crlset", {
			args: [`--log-net-log=${netLog}`],
		});

		// A first page, whose paint ends startup.
		const profile = await app.createProfile("Certificates");
		const first = context.fixtures.url("light.html?crlset");
		await app.page(await app.openTab(profile, first), first);

		const applied = await waitForEntry(
			"PaneEngine.crlSetApplied",
			() => true,
			60_000,
		);

		appliedAfter = applied.t - (trace()[0]?.t ?? applied.t);

		const tab = await app.openTab(profile, `https://127.0.0.1:${server.port}/`);

		await app.waitFor(
			"the certificate verified (and refused)",
			(state) => tabOf(state, tab)?.title === "Privacy error",
			30_000,
		);

		await app.browser.close();
	} finally {
		server.stop(true);
	}

	const log = JSON.parse(readFileSync(netLog, "utf8")) as NetLog;
	const verify = log.constants.logEventTypes.CERT_VERIFY_PROC;

	const sequences = [
		...new Set(
			log.events
				.filter((event) => event.type === verify)
				.map((event) => event.params?.crlset_sequence)
				.filter((sequence) => sequence !== undefined)
				.map(Number),
		),
	];

	expect(
		sequences.length > 0 && sequences.every((each) => each === header.Sequence),
		`the verifier used CRLSet ${JSON.stringify(sequences)}, the bundle's is ${header.Sequence}`,
	);

	return { bundled: header.Sequence, verified: sequences, appliedAfter };
};

export default crlset;
