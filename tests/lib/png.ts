// A PNG's pixels as its file holds them, and where two of one size differ:
// what a strict comparison of two runs' renders reads (rigs/compare-renders.ts),
// every byte of every pixel, no tolerance. It reads the 8-bit PNGs, not
// interlaced, that Pane's renders and the system's window captures are, in
// any color type, as RGBA; anything else it refuses, naming why.
import { inflateSync } from "node:zlib";

export type Pixels = { width: number; height: number; rgba: Uint8Array };

// One region where two images differ: the differing pixels of tiles that
// touch, joined. Its box in pixels, how many differ, and the largest
// difference of a channel, with both images' RGBA where it is.
export type Part = {
	x: number;
	y: number;
	width: number;
	height: number;
	pixels: number;
	largest: number;
	a: number[];
	b: number[];
};

const SIGNATURE = [137, 80, 78, 71, 13, 10, 26, 10];

// Samples per pixel, by color type: gray, RGB, palette, gray and alpha, RGBA.
const CHANNELS = new Map([
	[0, 1],
	[2, 3],
	[3, 1],
	[4, 2],
	[6, 4],
]);

export function readPng(bytes: Uint8Array): Pixels {
	if (SIGNATURE.some((byte, i) => bytes[i] !== byte)) {
		throw new Error("not a PNG");
	}

	const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
	let header: Uint8Array | undefined;
	let palette: Uint8Array | undefined;
	let transparency: Uint8Array | undefined;
	const data: Uint8Array[] = [];

	for (let at = 8; at + 8 <= bytes.length; ) {
		const length = view.getUint32(at);
		const name = String.fromCharCode(...bytes.subarray(at + 4, at + 8));
		const body = bytes.subarray(at + 8, at + 8 + length);

		if (name === "IHDR") {
			header = body;
		} else if (name === "PLTE") {
			palette = body;
		} else if (name === "tRNS") {
			transparency = body;
		} else if (name === "IDAT") {
			data.push(body);
		} else if (name === "IEND") {
			break;
		}

		at += 12 + length;
	}

	if (!header) {
		throw new Error("a PNG with no header");
	}

	const headerView = new DataView(header.buffer, header.byteOffset, 13);
	const width = headerView.getUint32(0);
	const height = headerView.getUint32(4);
	const [depth, type, , , interlace] = header.subarray(8, 13);
	const channels = CHANNELS.get(type ?? -1);

	if (depth !== 8 || channels === undefined || interlace !== 0) {
		throw new Error(
			`a PNG of depth ${depth}, color type ${type}, interlace ${interlace}: only 8-bit, not interlaced, is read`,
		);
	}

	const samples = unfilter(
		inflateSync(Buffer.concat(data)),
		width * channels,
		height,
		channels,
	);

	return {
		width,
		height,
		rgba: toRgba(samples, type ?? 0, palette, transparency),
	};
}

// The scanlines with each one's filter undone (the PNG specification's
// filters 0 to 4, `step` bytes to the pixel on the left).
function unfilter(
	filtered: Uint8Array,
	stride: number,
	height: number,
	step: number,
): Uint8Array {
	const out = new Uint8Array(stride * height);

	for (let y = 0; y < height; y++) {
		const filter = filtered[y * (stride + 1)];
		const from = y * (stride + 1) + 1;
		const row = y * stride;
		// The row above, all zeros for the first.
		const up = y > 0 ? out.subarray(row - stride, row) : new Uint8Array(stride);

		for (let x = 0; x < stride; x++) {
			const left = x >= step ? (out[row + x - step] as number) : 0;
			const above = up[x] as number;
			let predicted: number;

			switch (filter) {
				case 0:
					predicted = 0;

					break;
				case 1:
					predicted = left;

					break;
				case 2:
					predicted = above;

					break;
				case 3:
					predicted = (left + above) >> 1;

					break;
				case 4:
					predicted = paeth(
						left,
						above,
						x >= step ? (up[x - step] as number) : 0,
					);

					break;
				default:
					throw new Error(`a scanline with filter ${filter}`);
			}

			out[row + x] = ((filtered[from + x] as number) + predicted) & 0xff;
		}
	}

	return out;
}

// The PNG specification's Paeth predictor: of the bytes to the left, above,
// and above left, the one nearest their sum less the corner.
function paeth(left: number, above: number, corner: number): number {
	const p = left + above - corner;
	const toLeft = Math.abs(p - left);
	const toAbove = Math.abs(p - above);
	const toCorner = Math.abs(p - corner);

	if (toLeft <= toAbove && toLeft <= toCorner) {
		return left;
	}

	return toAbove <= toCorner ? above : corner;
}

function toRgba(
	samples: Uint8Array,
	type: number,
	palette: Uint8Array | undefined,
	transparency: Uint8Array | undefined,
): Uint8Array {
	if (type === 6) {
		return samples;
	}

	const channels = CHANNELS.get(type) ?? 1;
	const count = samples.length / channels;
	const rgba = new Uint8Array(count * 4);

	// The one color a gray or RGB image keeps transparent, if it names one:
	// 16-bit values, whose low byte an 8-bit image's sample is.
	const clear =
		transparency && (type === 0 || type === 2)
			? Array.from({ length: channels }, (_, i) => transparency[i * 2 + 1])
			: undefined;

	for (let i = 0; i < count; i++) {
		const at = i * channels;
		const to = i * 4;
		const first = samples[at] as number;

		if (type === 2) {
			rgba[to] = first;
			rgba[to + 1] = samples[at + 1] as number;
			rgba[to + 2] = samples[at + 2] as number;
			rgba[to + 3] = 255;
		} else if (type === 3) {
			rgba[to] = palette?.[first * 3] ?? 0;
			rgba[to + 1] = palette?.[first * 3 + 1] ?? 0;
			rgba[to + 2] = palette?.[first * 3 + 2] ?? 0;
			rgba[to + 3] = transparency?.[first] ?? 255;
		} else {
			rgba[to] = first;
			rgba[to + 1] = first;
			rgba[to + 2] = first;
			rgba[to + 3] = type === 4 ? (samples[at + 1] as number) : 255;
		}

		if (clear?.every((value, c) => value === samples[at + c])) {
			rgba[to + 3] = 0;
		}
	}

	return rgba;
}

// Where `a` and `b`, of one size, differ in any byte: their differing pixels
// grouped by `tile`-pixel squares, the squares that touch (sides or corners)
// joined into one part. None when every byte is the same.
export function differingParts(a: Pixels, b: Pixels, tile = 16): Part[] {
	const { width, height } = a;
	const across = Math.ceil(width / tile);
	const down = Math.ceil(height / tile);
	const hit = new Uint8Array(across * down);

	for (let i = 0; i < a.rgba.length; i += 4) {
		if (
			a.rgba[i] !== b.rgba[i] ||
			a.rgba[i + 1] !== b.rgba[i + 1] ||
			a.rgba[i + 2] !== b.rgba[i + 2] ||
			a.rgba[i + 3] !== b.rgba[i + 3]
		) {
			const pixel = i / 4;

			hit[
				Math.floor(pixel / width / tile) * across +
					Math.floor((pixel % width) / tile)
			] = 1;
		}
	}

	const parts: Part[] = [];
	const seen = new Uint8Array(across * down);

	for (let start = 0; start < hit.length; start++) {
		if (!hit[start] || seen[start]) {
			continue;
		}

		const tiles: number[] = [];
		const stack = [start];

		seen[start] = 1;

		for (let next = stack.pop(); next !== undefined; next = stack.pop()) {
			tiles.push(next);

			const tx = next % across;
			const ty = Math.floor(next / across);

			for (let dy = -1; dy <= 1; dy++) {
				for (let dx = -1; dx <= 1; dx++) {
					const nx = tx + dx;
					const ny = ty + dy;
					const n = ny * across + nx;

					if (
						nx >= 0 &&
						ny >= 0 &&
						nx < across &&
						ny < down &&
						hit[n] &&
						!seen[n]
					) {
						seen[n] = 1;
						stack.push(n);
					}
				}
			}
		}

		parts.push(partOf(a, b, tiles, tile, across));
	}

	return parts;
}

function partOf(
	a: Pixels,
	b: Pixels,
	tiles: number[],
	tile: number,
	across: number,
): Part {
	let [left, top, right, bottom] = [Infinity, Infinity, -1, -1];
	let pixels = 0;
	let largest = 0;
	let at = 0;

	for (const t of tiles) {
		const x0 = (t % across) * tile;
		const y0 = Math.floor(t / across) * tile;

		for (let y = y0; y < Math.min(a.height, y0 + tile); y++) {
			for (let x = x0; x < Math.min(a.width, x0 + tile); x++) {
				const i = (y * a.width + x) * 4;
				let difference = 0;

				for (let c = 0; c < 4; c++) {
					difference = Math.max(
						difference,
						Math.abs((a.rgba[i + c] ?? 0) - (b.rgba[i + c] ?? 0)),
					);
				}

				if (difference > 0) {
					pixels++;
					left = Math.min(left, x);
					top = Math.min(top, y);
					right = Math.max(right, x);
					bottom = Math.max(bottom, y);

					if (difference > largest) {
						largest = difference;
						at = i;
					}
				}
			}
		}
	}

	return {
		x: left,
		y: top,
		width: right - left + 1,
		height: bottom - top + 1,
		pixels,
		largest,
		a: Array.from(a.rgba.subarray(at, at + 4)),
		b: Array.from(b.rgba.subarray(at, at + 4)),
	};
}
