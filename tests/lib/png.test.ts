import { expect, test } from "bun:test";
import { deflateSync } from "node:zlib";
import { differingParts, type Pixels, readPng } from "./png";

// A PNG of `rows`, each a filter byte then its filtered samples, written as
// an encoder writes one (the reader checks no CRC, so they're zeros).
function png(
	width: number,
	height: number,
	type: number,
	rows: number[][],
	extra: [string, number[]][] = [],
): Uint8Array {
	const chunk = (name: string, body: Uint8Array) => {
		const out = new Uint8Array(12 + body.length);
		const view = new DataView(out.buffer);

		view.setUint32(0, body.length);
		out.set(Buffer.from(name, "latin1"), 4);
		out.set(body, 8);

		return out;
	};

	const header = new Uint8Array(13);
	const view = new DataView(header.buffer);

	view.setUint32(0, width);
	view.setUint32(4, height);
	header.set([8, type, 0, 0, 0], 8);

	return Buffer.concat([
		Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
		chunk("IHDR", header),
		...extra.map(([name, body]) => chunk(name, Uint8Array.from(body))),
		chunk("IDAT", deflateSync(Uint8Array.from(rows.flat()))),
		chunk("IEND", new Uint8Array()),
	]);
}

// Two RGB pixels a row, three rows: (10,20,30) (40,50,60), (11,22,33)
// (44,55,66), (12,24,36) (48,60,72), under each filter in turn.
const RGB = [
	[10, 20, 30, 40, 50, 60],
	[11, 22, 33, 44, 55, 66],
	[12, 24, 36, 48, 60, 72],
];

function rgba(rows: number[][]): number[] {
	return rows.flatMap((row) =>
		[0, 3].flatMap((at) => [...row.slice(at, at + 3), 255]),
	);
}

test("each filter undone", () => {
	// None; Sub (each byte less the pixel's to its left); Up (less the one
	// above); Average; Paeth.
	const none = [0, ...(RGB[0] ?? [])];
	const sub = [1, 11, 22, 33, 44 - 11, 55 - 22, 66 - 33];
	const up = [2, 12 - 11, 24 - 22, 36 - 33, 48 - 44, 60 - 55, 72 - 66];

	expect(Array.from(readPng(png(2, 3, 2, [none, sub, up])).rgba)).toEqual(
		rgba(RGB),
	);

	// Average: the first pixel less half the one above, the second less half
	// its left and above together. Paeth, on row 1 over row 0: the first
	// pixel's predictor is the byte above (nothing left), the second's the
	// nearest of left, above, and corner to left + above - corner.
	const average = [
		3,
		11 - 5,
		22 - 10,
		33 - 15,
		44 - ((11 + 40) >> 1),
		55 - ((22 + 50) >> 1),
		66 - ((33 + 60) >> 1),
	];

	const paeth = [4, 11 - 10, 22 - 20, 33 - 30, 44 - 40, 55 - 50, 66 - 60];

	expect(
		Array.from(readPng(png(2, 3, 2, [none, average, up])).rgba).slice(8, 16),
	).toEqual(rgba(RGB).slice(8, 16));

	expect(
		Array.from(readPng(png(2, 3, 2, [none, paeth, up])).rgba).slice(8, 16),
	).toEqual(rgba(RGB).slice(8, 16));
});

test("a palette's colors, and its transparency", () => {
	const image = png(
		2,
		1,
		3,
		[[0, 1, 0]],
		[
			["PLTE", [255, 0, 0, 0, 0, 255]],
			["tRNS", [128]],
		],
	);

	expect(Array.from(readPng(image).rgba)).toEqual([
		0, 0, 255, 255, 255, 0, 0, 128,
	]);
});

test("16-bit and interlaced PNGs are refused", () => {
	const deep = png(1, 1, 2, [[0, 1, 2, 3]]);

	const interlaced = png(1, 1, 2, [[0, 1, 2, 3]]);

	deep[24] = 16;
	interlaced[28] = 1;
	expect(() => readPng(deep)).toThrow("only 8-bit");
	expect(() => readPng(interlaced)).toThrow("only 8-bit, not interlaced");
});

function filled(width: number, height: number, value: number): Pixels {
	return {
		width,
		height,
		rgba: new Uint8Array(width * height * 4).fill(value),
	};
}

test("one part for pixels that touch, one for each apart", () => {
	const a = filled(64, 64, 200);
	const b = filled(64, 64, 200);

	expect(differingParts(a, b)).toEqual([]);

	// Two pixels in squares side by side, and one far off.
	for (const [x, y, value] of [
		[15, 3, 190],
		[16, 3, 150],
		[60, 60, 199],
	] as const) {
		b.rgba[(y * 64 + x) * 4] = value;
	}

	const parts = differingParts(a, b);

	expect(
		parts.map(({ x, y, width, height, pixels, largest }) => ({
			x,
			y,
			width,
			height,
			pixels,
			largest,
		})),
	).toEqual([
		{ x: 15, y: 3, width: 2, height: 1, pixels: 2, largest: 50 },
		{ x: 60, y: 60, width: 1, height: 1, pixels: 1, largest: 1 },
	]);

	expect(parts[0]?.a).toEqual([200, 200, 200, 200]);
	expect(parts[0]?.b).toEqual([150, 200, 200, 200]);
});
