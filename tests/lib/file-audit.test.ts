import { expect, test } from "bun:test";
import { plainMarkers } from "./file-audit";

// A LevelDB CURRENT that the engine wrote in a profile, from the probe of
// 2026-11-15 (evidence/views-migration/runs/p5-fmig-current-notes.md): the
// stream header (its magic, a random file id, and a check that matches the
// profile's key), then MANIFEST-000002 under AES-256-CTR. 13 of those 16 bytes
// are text's, so the audit read the file as plain.
const ENGINE_CURRENT = Buffer.from(
	"50414e455346310af353da3a90eb77fe298d43ebac845e462fdb14c8247b9ba03d6e480a234e3a1d7a09901876734055",
	"hex",
);

const STREAM_HEADER = ENGINE_CURRENT.subarray(0, 32);
const MANIFEST = Buffer.from("MANIFEST-000002\n", "latin1");
const WHOLE_FILE_MAGIC = Buffer.from("PANEWF1\n", "latin1");

test("short ciphertext past a stream's header is no text", () => {
	expect(plainMarkers("CURRENT", ENGINE_CURRENT)).toEqual([]);
});

test("a stream's header over clear content is text", () => {
	expect(
		plainMarkers("CURRENT", Buffer.concat([STREAM_HEADER, MANIFEST])),
	).toEqual(["text"]);
});

test("a plain CURRENT is text", () => {
	expect(plainMarkers("CURRENT", MANIFEST)).toEqual(["text"]);
});

// A sealed empty file, as Pane's markers beside a profile are: the whole file's
// magic, then its nonce and tag, random. These hold 22 bytes of text in 28,
// which random bytes do once in 60,000; with the magic's 8, the audit read 30
// of 36 as text.
test("a whole file's magic counts as no text of its content", () => {
	const sealed = Buffer.concat([
		WHOLE_FILE_MAGIC,
		Buffer.from(
			"4183583e3fbe2229b9a853625bd31d763d54646f60332a270d4e7360",
			"hex",
		),
	]);

	expect(plainMarkers(".files-encrypted", sealed)).toEqual([]);
});

test("a whole file's magic over clear content is text", () => {
	const preferences = Buffer.concat([
		WHOLE_FILE_MAGIC,
		Buffer.from('{"browser":{"window_placement":{"bottom":900}}}', "latin1"),
	]);

	expect(plainMarkers("Preferences", preferences)).toContain("text");
});
