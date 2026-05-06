import { StrictMode } from "react";
import { createRoot } from "react-dom/client";

import { ErrorBoundary, Layout } from "./app/layout";
import { SurfaceLayout } from "./app/surface";
import "./styles/globals.css";

const root = document.getElementById("root");

if (!root) {
	throw new Error("Root element not found");
}

const isSurface = new URLSearchParams(window.location.search).has("surface");

if (isSurface) {
	document.documentElement.style.background = "transparent";
	document.body.style.background = "transparent";
	document.getElementById("splash")?.remove();
}

function dismissSplash() {
	const splash = document.getElementById("splash");

	if (!splash) {
		return;
	}

	setTimeout(() => {
		splash.classList.add("fade-out");
		splash.addEventListener("transitionend", () => splash.remove());
	}, 600);
}

createRoot(root).render(
	<StrictMode>
		{isSurface ? (
			<SurfaceLayout />
		) : (
			<ErrorBoundary>
				<Layout onReady={dismissSplash} />
			</ErrorBoundary>
		)}
	</StrictMode>,
);
