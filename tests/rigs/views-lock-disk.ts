import { rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { audit } from "../lib/file-audit";
import { setUpPin } from "../lib/pane";
import { poll } from "../lib/poll";
import {
	plantedInRealTemporaryFolder,
	READ_BACK,
	type ReadBack,
	richSession,
	STORES_COMMITTED_MS,
} from "../lib/rich-session";
import { imagesAttachedIn, mountedAt } from "../lib/tools";
import { expect, type Scenario } from "../run";

// L3's disk check behind the Lock (docs/plans/views-migration.md, Phase 3;
// memory: check claims by grepping disk): a rich session on a host of its own
// (cookies, both storages, IndexedDB and a blob, CacheStorage, a service
// worker, the origin private file system, an extension's storage, a
// download), the PIN set, then Lock Pane. Once the volume has detached, every
// file of the data root, the throwaway home, and the Mac's temporary folder is
// read: none holds a planted value, its host, or its pages, in clear or as an
// unkeyed hash, and none reads as plain where ADR 0121 says ciphertext. Two
// controls: a file planted in the data root holding one of the values, which
// the same audit must find; and the PIN bringing the session back, read
// through its page, so what was looked for was there to find. Run against the
// Views build and the published Pane alike (--binary): the same host, the
// same session.
const lockDisk: Scenario = async (context) => {
	const session = await richSession(context);
	const { app, planted, roots } = session;
	await Bun.sleep(STORES_COMMITTED_MS);

	await setUpPin(app, "2468");
	await app.send("lockNow");

	await app.waitFor(
		"the data closed behind the Lock",
		(state) =>
			state.security.locked &&
			!state.security.data.attached &&
			state.profiles.length === 0,
		30_000,
	);

	// The data's state says detached as the Lock starts, as the Swift's did;
	// the volume itself goes once every profile has closed.
	const mounted = await poll(
		"the volume detached",
		async () =>
			!(await mountedAt(join(roots.dataRoot, "Engine", "Volume"))) &&
			(await imagesAttachedIn(roots.dataRoot)).length === 0,
		30_000,
	).then(
		() => false,
		() => true,
	);

	const locked = audit(roots, planted);
	const inTemporaryFolder = await plantedInRealTemporaryFolder(planted);

	// The control: one of the values, planted where the audit reads.
	const control = join(roots.dataRoot, "control-planted.txt");
	writeFileSync(control, `control ${planted.values.localStorage}\n`);
	const withControl = audit(roots, planted);
	rmSync(control, { force: true });

	const controlFound = withControl.planted.some((file) =>
		file.path.endsWith("control-planted.txt"),
	);

	// The PIN brings it all back.
	await app.send("pinKeys", { keys: "2468" });

	await app.waitFor(
		"open again, the tab back",
		(state) =>
			!state.security.locked &&
			state.security.data.attached &&
			state.activeTabID === session.tab,
		60_000,
	);

	const again = await app.page(session.tab, session.url);
	const back = await again.evaluate<ReadBack>(READ_BACK);
	await app.quit();

	const report = {
		files: locked.files.length,
		mounted,
		plain: locked.plain.map(({ path, kind, plain }) => ({ path, kind, plain })),
		planted: locked.planted.map(({ path, planted }) => ({ path, planted })),
		named: locked.named,
		inTemporaryFolder,
		controlFound,
		readBack:
			back.localStorage === planted.values.localStorage &&
			back.indexedDB.value === planted.values.indexedDB &&
			back.cacheStorage === planted.values.cacheStorage &&
			back.originPrivate === planted.values.originPrivate &&
			back.cookie.includes(planted.values.scriptCookie ?? ""),
		byKind: Object.fromEntries(
			[...new Set(locked.files.map((file) => file.kind))]
				.sort()
				.map((kind) => [
					kind,
					locked.files.filter((file) => file.kind === kind).length,
				]),
		),
	};

	writeFileSync(
		join(context.out, "lock-disk-findings.json"),
		`${JSON.stringify(report, null, "\t")}\n`,
	);

	expect(!mounted, "the volume is mounted behind the Lock");
	expect(report.controlFound, "the audit missed the control's planted value");

	expect(
		report.readBack,
		`the session did not come back: ${JSON.stringify(back)}`,
	);

	expect(
		report.plain.length === 0 &&
			report.planted.length === 0 &&
			report.named.length === 0 &&
			report.inTemporaryFolder.length === 0,
		`behind the Lock: ${JSON.stringify({ plain: report.plain, planted: report.planted, named: report.named, temporary: report.inTemporaryFolder })}`,
	);

	return report;
};

export default lockDisk;
