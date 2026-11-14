// A profile's files as Pane's engine reads them (ADR 0121), for a scenario that
// checks what they hold: the mock keychain's key opens this way, and in the
// test VM the user's own (lib/engine-key.ts's userSecret). The chunked
// format (the caches) stays closed: it takes AES-GCM-SIV, and its entries' names
// are keyed.
import { createCipheriv, createDecipheriv, hkdfSync } from "node:crypto";
import { readFileSync } from "node:fs";
import { engineKey } from "./engine-key";

const SQLITE_MAGIC = Buffer.from("Pane SQLite v1\n\0", "latin1");
const SQLITE_PLAIN = Buffer.from("SQLite format 3\0", "latin1");
const STREAM_MAGIC = Buffer.from("PANESF1\n", "latin1");
const WHOLE_MAGIC = Buffer.from("PANEWF1\n", "latin1");

const derive = (key: Buffer, salt: Buffer, info: string) =>
	Buffer.from(hkdfSync("sha256", key, salt, info, 32));

export type ProfileKeys = { sqlite: Buffer; stream: Buffer; whole: Buffer };

export function profileKeys(
	dataDir: string,
	profile: string,
	pin: string | null = null,
	secret?: Buffer,
): ProfileKeys {
	const key = engineKey(dataDir, profile, pin, secret);
	const none = Buffer.alloc(0);

	return {
		sqlite: derive(key, none, "Pane SQLite pages"),
		stream: derive(key, none, "Pane stream files"),
		whole: derive(key, none, "Pane whole files"),
	};
}

function openSqlite(bytes: Buffer, key: Buffer): Buffer {
	const size = (bytes[16] ?? 0) * 256 + (bytes[17] ?? 0) * 65536;
	const plain = Buffer.alloc(bytes.length);

	for (let page = 0; page * size < bytes.length; page++) {
		const stored = bytes.subarray(page * size, (page + 1) * size);

		if (stored.every((byte) => byte === 0)) {
			continue;
		}

		const contentEnd = size - 32;
		const isFirst = page === 0;
		const start = isFirst ? 24 : 0;
		const place = Buffer.alloc(8);
		place.writeBigUInt64LE(BigInt(page + 1));

		const decipher = createDecipheriv(
			"aes-256-gcm",
			key,
			stored.subarray(contentEnd, contentEnd + 12),
		);

		decipher.setAuthTag(stored.subarray(contentEnd + 12, contentEnd + 28));

		decipher.setAAD(
			Buffer.concat([
				Buffer.from([0]),
				place,
				stored.subarray(contentEnd + 28, size),
				...(isFirst ? [stored.subarray(16, 24)] : []),
			]),
		);

		const opened = Buffer.concat([
			decipher.update(stored.subarray(start, contentEnd)),
			decipher.final(),
		]);

		opened.copy(plain, page * size + start);

		if (isFirst) {
			SQLITE_PLAIN.copy(plain, 0);
			stored.copy(plain, 16, 16, 24);
		}
	}

	return plain;
}

function openStream(bytes: Buffer, key: Buffer): Buffer {
	const fileKey = derive(key, bytes.subarray(8, 24), "Pane stream file");
	const cipher = createCipheriv("aes-256-ctr", fileKey, Buffer.alloc(16));

	return Buffer.concat([cipher.update(bytes.subarray(32)), cipher.final()]);
}

function openWhole(bytes: Buffer, key: Buffer): Buffer {
	const body = bytes.subarray(WHOLE_MAGIC.length + 12);

	const decipher = createDecipheriv(
		"aes-256-gcm",
		key,
		bytes.subarray(WHOLE_MAGIC.length, WHOLE_MAGIC.length + 12),
	);

	decipher.setAuthTag(body.subarray(-16));
	decipher.setAAD(WHOLE_MAGIC);

	return Buffer.concat([
		decipher.update(body.subarray(0, -16)),
		decipher.final(),
	]);
}

// What the file holds; as it is, for a format not its own, or one of the caches'.
export function plainContent(path: string, keys: ProfileKeys): Buffer {
	const bytes = readFileSync(path);

	if (bytes.subarray(0, SQLITE_MAGIC.length).equals(SQLITE_MAGIC)) {
		return openSqlite(bytes, keys.sqlite);
	}

	if (bytes.subarray(0, STREAM_MAGIC.length).equals(STREAM_MAGIC)) {
		return openStream(bytes, keys.stream);
	}

	if (bytes.subarray(0, WHOLE_MAGIC.length).equals(WHOLE_MAGIC)) {
		return openWhole(bytes, keys.whole);
	}

	return bytes;
}
