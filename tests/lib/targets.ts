// The browser's DevTools targets (pages, popups, workers, DevTools itself),
// found by what they show, and attached to.
import { type Cdp, openPage, type Page } from "./cdp";
import { poll } from "./poll";

export type Target = {
	targetId: string;
	type: string;
	url: string;
	title: string;
	browserContextId?: string;
};

export async function targets(cdp: Cdp): Promise<Target[]> {
	return (await cdp.send<{ targetInfos: Target[] }>("Target.getTargets"))
		.targetInfos;
}

// The first target that `matches`, once there is one.
export async function waitForTarget(
	cdp: Cdp,
	description: string,
	matches: (target: Target) => boolean,
	timeoutMs = 30_000,
): Promise<Target> {
	return poll(
		`a target: ${description}`,
		async () => (await targets(cdp)).find(matches),
		timeoutMs,
		250,
	);
}

// Attaches to the first page target that `matches`, once there is one.
export async function waitForPage(
	cdp: Cdp,
	description: string,
	matches: (target: Target) => boolean,
	timeoutMs = 30_000,
): Promise<Page> {
	const target = await waitForTarget(
		cdp,
		description,
		(info) => info.type === "page" && matches(info),
		timeoutMs,
	);

	return openPage(cdp, "about:blank", target.targetId);
}
