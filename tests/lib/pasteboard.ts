// The Mac's clipboard, which a page's copy commands write: read it, and put
// back what was there before a scenario wrote it (tools/pasteboard.swift).
import { join } from "node:path";
import { swiftTool } from "./tools";

async function pasteboard(...args: string[]): Promise<string> {
	return Bun.$`${await swiftTool("pasteboard")} ${args}`.text();
}

export function clipboardText(): Promise<string> {
	return pasteboard("text");
}

export async function clearClipboard(): Promise<void> {
	await pasteboard("clear");
}

// Saves the clipboard into `directory` (a scenario's own, removed when it
// ends) and returns what puts it back.
export async function saveClipboard(
	directory: string,
): Promise<() => Promise<void>> {
	const file = join(directory, "clipboard.json");
	await pasteboard("save", file);

	return async () => {
		await pasteboard("restore", file);
	};
}
