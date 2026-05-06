type IconNode = [string, Record<string, string>][];

const ICON_SIZE = 16;
const cache = new Map<string, Promise<string>>();

function renderToPng(svgMarkup: string): Promise<string> {
	return new Promise((resolve) => {
		const image = new Image();
		const blob = new Blob([svgMarkup], { type: "image/svg+xml" });
		const url = URL.createObjectURL(blob);

		image.onload = () => {
			const canvas = document.createElement("canvas");
			canvas.width = ICON_SIZE;
			canvas.height = ICON_SIZE;

			const context = canvas.getContext("2d") as CanvasRenderingContext2D;
			context.drawImage(image, 0, 0, ICON_SIZE, ICON_SIZE);

			URL.revokeObjectURL(url);
			resolve(canvas.toDataURL("image/png"));
		};

		image.src = url;
	});
}

function buildSvg(content: string, color: string): string {
	return `<svg xmlns="http://www.w3.org/2000/svg" width="${ICON_SIZE}" height="${ICON_SIZE}" viewBox="0 0 24 24" fill="none" stroke="${color}" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">${content}</svg>`;
}

function iconNodeToSvgContent(iconNode: IconNode): string {
	return iconNode
		.map(([tag, attrs]) => {
			const pairs = Object.entries(attrs)
				.filter(([key]) => key !== "key")
				.map(([key, value]) => `${key}="${value}"`)
				.join(" ");

			return `<${tag} ${pairs}/>`;
		})
		.join("");
}

export function menuIcon(
	iconNode: IconNode,
	color = "#000000",
): Promise<string> {
	const svgContent = iconNodeToSvgContent(iconNode);
	const cacheKey = `${svgContent}:${color}`;
	const cached = cache.get(cacheKey);

	if (cached) {
		return cached;
	}

	const promise = renderToPng(buildSvg(svgContent, color));
	cache.set(cacheKey, promise);

	return promise;
}

export const icons = {
	pencil: [
		[
			"path",
			{
				d: "M21.174 6.812a1 1 0 0 0-3.986-3.987L3.842 16.174a2 2 0 0 0-.5.83l-1.321 4.352a.5.5 0 0 0 .623.622l4.353-1.32a2 2 0 0 0 .83-.497z",
			},
		],
		["path", { d: "m15 5 4 4" }],
	],
	trash: [
		["path", { d: "M10 11v6" }],
		["path", { d: "M14 11v6" }],
		["path", { d: "M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6" }],
		["path", { d: "M3 6h18" }],
		["path", { d: "M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2" }],
	],
} satisfies Record<string, IconNode>;
