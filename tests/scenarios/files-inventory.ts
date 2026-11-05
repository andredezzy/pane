import { writeFileSync } from "node:fs";
import { join } from "node:path";
import { audit } from "../lib/file-audit";
import { richSession, STORES_COMMITTED_MS } from "../lib/rich-session";
import { expect, type Scenario } from "../run";

// Every file a rich session leaves, with Pane open and unlocked: its kind, what
// wrote it, and whether its content reads as plain. The list is the coverage
// checklist of docs/plans/engine-file-encryption.md, and engine/UPGRADING.md
// runs it at each new Chromium, to see the stores a milestone adds.
const filesInventory: Scenario = async (context) => {
	const session = await richSession(context);
	await Bun.sleep(STORES_COMMITTED_MS);
	const found = audit(session.roots, session.planted);

	expect(found.files.length > 0, "the session left no files");

	const inventory = found.files
		.map(
			({
				path,
				size,
				kind,
				writer,
				allowed,
				plain,
				entropy,
				planted,
				named,
			}) => ({
				path: path.replace(
					/[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}/gi,
					"<id>",
				),
				size,
				kind,
				writer,
				allowed,
				plain,
				entropy,
				planted,
				named,
			}),
		)
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

export default filesInventory;
