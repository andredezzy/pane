import { writeFileSync } from "node:fs";
import { join } from "node:path";
import { audit } from "../lib/file-audit";
import {
	plantedInRealTemporaryFolder,
	READ_BACK,
	type ReadBack,
	richSession,
	STORES_COMMITTED_MS,
} from "../lib/rich-session";
import { expect, type Scenario } from "../run";

// What a process that reads files finds while Pane is open and unlocked
// (docs/adr/0121): every file under each profile's
// folder, its caches, the data root, and what Pane's processes made in $TMPDIR
// is ciphertext or on ADR 0121's allow-list; none holds a planted value; and no
// name gives a site away, in clear or as an unkeyed hash.
//
// It also lists every file, with its kind, what wrote it, and whether it reads
// as plain, in files-inventory-list.json: chromium/UPGRADING.md reads the result's
// `unknown` at each new Chromium, to see the stores a milestone adds.
const filesEncrypted: Scenario = async (context) => {
	const session = await richSession(context);
	await Bun.sleep(STORES_COMMITTED_MS);
	const found = audit(session.roots, session.planted);
	const inTemporaryFolder = await plantedInRealTemporaryFolder(session.planted);

	// What it wrote still reads, through everything that encrypted it.
	const back = await session.page.evaluate<ReadBack>(READ_BACK);
	const { values } = session.planted;

	expect(
		back.localStorage === values.localStorage &&
			back.sessionStorage === values.sessionStorage &&
			back.indexedDB.value === values.indexedDB &&
			back.indexedDB.blob.startsWith(values.indexedDBBlob ?? "") &&
			back.cacheStorage === values.cacheStorage &&
			back.originPrivate === values.originPrivate &&
			back.originPrivateSync === values.originPrivateSync &&
			back.serviceWorker !== null &&
			back.cookie.includes(values.scriptCookie ?? ""),
		`the session does not read back: ${JSON.stringify(back)}`,
	);

	const report = {
		files: found.files.length,
		plain: found.plain.map(({ path, kind, plain }) => ({ path, kind, plain })),
		planted: found.planted.map(({ path, planted }) => ({ path, planted })),
		named: found.named,
		inTemporaryFolder,
	};

	writeFileSync(
		join(context.out, "files-encrypted-findings.json"),
		`${JSON.stringify(report, null, "\t")}\n`,
	);

	const inventory = found.files
		.map((file) => ({
			...file,
			path: file.path.replace(
				/[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}/gi,
				"<id>",
			),
		}))
		.sort((a, b) => a.path.localeCompare(b.path));

	writeFileSync(
		join(context.out, "files-inventory-list.json"),
		`${JSON.stringify(inventory, null, "\t")}\n`,
	);

	const byKind: Record<
		string,
		{ files: number; bytes: number; plain: number }
	> = {};

	for (const file of found.files) {
		const entry = byKind[file.kind] ?? { files: 0, bytes: 0, plain: 0 };
		byKind[file.kind] = entry;
		entry.files++;
		entry.bytes += file.size;
		entry.plain += file.plain.length > 0 ? 1 : 0;
	}

	await session.app.quit();

	// A session that left nothing to find would pass anything.
	expect(found.files.length > 50, `only ${found.files.length} files`);

	expect(
		inTemporaryFolder.length === 0,
		`planted values in the temporary folder: ${inTemporaryFolder.join("; ")}`,
	);

	expect(
		found.plain.length === 0,
		`plain files: ${found.plain.map((file) => `${file.path} (${file.plain.join(", ")})`).join("; ")}`,
	);

	expect(
		found.planted.length === 0,
		`planted values on disk: ${found.planted.map((file) => `${file.path} (${file.planted.join(", ")})`).join("; ")}`,
	);

	expect(
		found.named.length === 0,
		`names that give a site away: ${found.named.map((item) => `${item.path} (${item.named.join(", ")})`).join("; ")}`,
	);

	return {
		files: found.files.length,
		directories: found.directories.length,
		byKind,
		unknown: found.files
			.filter((file) => file.kind === "unknown")
			.map((file) => file.path),
		sessions: found.files
			.filter((file) => /\/Sessions\/Session_/.test(file.path))
			.map((file) => file.path),
		temporary: found.files
			.filter((file) => file.path.startsWith("<tmp>/"))
			.map((file) => file.path),
	};
};

export default filesEncrypted;
