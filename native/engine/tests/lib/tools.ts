// The Swift tools in tools/, compiled on first use, and again once their
// source is newer than the binary.
import { existsSync, mkdirSync, statSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const BINARIES = join(tmpdir(), "pane-tests");

export async function swiftTool(name: string): Promise<string> {
	const source = join(import.meta.dir, "..", "tools", `${name}.swift`);
	const binary = join(BINARIES, name);

	if (
		!existsSync(binary) ||
		statSync(binary).mtimeMs < statSync(source).mtimeMs
	) {
		mkdirSync(BINARIES, { recursive: true });
		await Bun.$`swiftc -O ${source} -o ${binary}`.quiet();
	}

	return binary;
}
