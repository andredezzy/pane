import type { ComponentType } from "react";
import { trpc } from "./trpc";

// biome-ignore lint/suspicious/noExplicitAny: accepts any component
type AnyComponent = ComponentType<any>;

export const surface = {
	open(component: AnyComponent) {
		trpc.ui.present.mutate({ name: component.name });
	},
	close() {
		trpc.ui.dismiss.mutate();
	},
};
