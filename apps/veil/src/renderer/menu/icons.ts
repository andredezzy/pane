import {
	type ComponentType,
	createElement,
	isValidElement,
	type ReactElement,
} from "react";
import { renderToStaticMarkup } from "react-dom/server";

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

// biome-ignore lint/suspicious/noExplicitAny: accepts any icon component
type IconComponent = ComponentType<any>;

export function menuIcon(icon: IconComponent | ReactElement): Promise<string> {
	const element = isValidElement(icon)
		? icon
		: createElement(icon, {
				size: ICON_SIZE,
				strokeWidth: 1.5,
				color: "#000000",
			});

	const svgMarkup = renderToStaticMarkup(element);

	const cached = cache.get(svgMarkup);
	if (cached) {
		return cached;
	}

	const promise = renderToPng(svgMarkup);
	cache.set(svgMarkup, promise);

	return promise;
}
