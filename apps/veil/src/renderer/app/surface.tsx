import { type ComponentType, useEffect, useRef, useState } from "react";
import { surface } from "../surface";

// biome-ignore lint/suspicious/noExplicitAny: registry accepts any component
type AnyComponent = ComponentType<any>;

const modules = (
	import.meta as unknown as {
		glob: (p: string[], o: object) => Record<string, Record<string, unknown>>;
	}
).glob(["../**/*.tsx"], { eager: true });

const registry = new Map<string, AnyComponent>();
for (const mod of Object.values(modules)) {
	for (const value of Object.values(mod)) {
		if (typeof value === "function" && value.name) {
			registry.set(value.name, value as AnyComponent);
		}
	}
}

interface ActiveSurface {
	name: string;
	key: number;
}

export function SurfaceLayout() {
	const [active, setActive] = useState<ActiveSurface | null>(null);
	const keyRef = useRef(0);

	useEffect(() => {
		const handle = (e: MessageEvent) => {
			if (typeof e.data?.name === "string" && registry.has(e.data.name)) {
				keyRef.current++;
				setActive({ name: e.data.name, key: keyRef.current });
			}
		};

		window.addEventListener("message", handle);

		return () => window.removeEventListener("message", handle);
	}, []);

	if (!active) {
		return null;
	}

	const Component = registry.get(active.name);

	if (!Component) {
		return null;
	}

	return <Component key={active.key} onClose={surface.close} />;
}
