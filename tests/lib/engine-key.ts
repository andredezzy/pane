// A profile's engine key, as the app derives it (PaneKit DataKey, ADR 0121), from
// the sealed data key under the mock keychain's secret: for a scenario to look
// for the key where it must not be. Only the mock keychain's key opens this way.
import {
	createDecipheriv,
	createHash,
	hkdfSync,
	pbkdf2Sync,
} from "node:crypto";
import { readFileSync } from "node:fs";
import { join } from "node:path";

const MOCK_SECRET = Buffer.from("Pane mock keychain");

type Sealed = {
	box: string;
	pin: { salt: string; rounds: number } | null;
};

function dataKey(dataDir: string, pin: string | null): Buffer {
	const sealed = (
		JSON.parse(readFileSync(join(dataDir, "security.json"), "utf8")) as {
			state: { key: Sealed };
		}
	).state.key;

	const salt =
		pin && sealed.pin
			? pbkdf2Sync(
					pin,
					Buffer.from(sealed.pin.salt, "base64"),
					sealed.pin.rounds,
					32,
					"sha256",
				)
			: Buffer.alloc(0);

	const sealing = Buffer.from(
		hkdfSync("sha256", MOCK_SECRET, salt, "Pane data key", 32),
	);

	const box = Buffer.from(sealed.box, "base64");

	const decipher = createDecipheriv(
		"aes-256-gcm",
		sealing,
		box.subarray(0, 12),
	);

	decipher.setAuthTag(box.subarray(-16));

	return Buffer.concat([
		decipher.update(box.subarray(12, -16)),
		decipher.final(),
	]);
}

export function engineKey(
	dataDir: string,
	profile: string,
	pin: string | null,
): Buffer {
	return Buffer.from(
		hkdfSync(
			"sha256",
			dataKey(dataDir, pin),
			Buffer.from(profile),
			"Pane engine files",
			32,
		),
	);
}

// Each form a key could be found in, by what it is.
export function keyForms(key: Buffer): Buffer[] {
	return [
		key,
		Buffer.from(key.toString("hex")),
		Buffer.from(key.toString("base64")),
		createHash("sha256").update(key).digest(),
	];
}
