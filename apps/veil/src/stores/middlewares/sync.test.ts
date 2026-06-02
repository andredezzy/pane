import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { createStore } from "zustand/vanilla";

import { registerSyncTransport, type SyncTransport, sync } from "./sync";

// The fake transport is the seam that replaces the renderer's tRPC-over-IPC
// client, so we can capture exactly what the sync middleware pushes to main.
const pushMock = vi.fn((_input: { name: string; state: string }) =>
	Promise.resolve(),
);

const subscribeMock = vi.fn();

const fakeTransport: SyncTransport = {
	push: pushMock,
	subscribe: subscribeMock,
};

interface Counter {
	owned: number;
	shared: number;
	bumpShared: () => void;
}

function createCounter(pushPartialize?: (state: Counter) => Partial<Counter>) {
	return createStore<Counter>()(
		sync<Counter>(
			(set) => ({
				owned: 3,
				shared: 0,
				bumpShared: () => set((state) => ({ shared: state.shared + 1 })),
			}),
			{ name: "settings-store", pushPartialize },
		),
	);
}

beforeEach(() => {
	// fs-storage detects the renderer via `window`; keep its disk writes a no-op.
	vi.stubGlobal("window", {});
	pushMock.mockClear();
	subscribeMock.mockClear();
	registerSyncTransport(fakeTransport);
});

afterEach(() => {
	vi.unstubAllGlobals();
});

describe("sync middleware push payload", () => {
	it("pushes only the pushPartialize fields, omitting backend-owned state", () => {
		const store = createCounter((state) => ({ shared: state.shared }));

		store.getState().bumpShared();

		expect(pushMock).toHaveBeenCalledTimes(1);
		expect(JSON.parse(pushMock.mock.calls[0][0].state)).toEqual({ shared: 1 });
	});

	it("pushes the full state when no pushPartialize is given (pre-fix behavior)", () => {
		const store = createCounter();

		store.getState().bumpShared();

		// Without a filter, the stale `owned` field rides along — this is the
		// shape that clobbered failedAttempts before the fix.
		const pushed = JSON.parse(pushMock.mock.calls[0][0].state);
		expect(pushed.owned).toBe(3);
		expect(pushed.shared).toBe(1);
	});
});

describe("securityStore — regression: PIN attempts reset clobber", () => {
	it("does not push failedAttempts or pin back to main on unlock", async () => {
		const { securityStore } = await import("../security-store");

		// The diverged moment: prior failed attempts recorded by main, screen
		// still locked, broadcast of the upcoming reset not yet received.
		securityStore.setState({ failedAttempts: 3, isLocked: true });
		pushMock.mockClear();

		securityStore.getState().unlock();

		expect(pushMock).toHaveBeenCalledTimes(1);

		const { name, state } = pushMock.mock.calls[0][0];
		const pushed = JSON.parse(state);

		expect(name).toBe("security-store");
		// The crux: a stale failedAttempts can no longer overwrite the reset
		// main applied moments earlier.
		expect(pushed).not.toHaveProperty("failedAttempts");
		expect(pushed).not.toHaveProperty("pin");
		expect(pushed.isLocked).toBe(false);
	});
});
