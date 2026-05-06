import type { ComponentProps, ComponentType } from "react";
import { trpc } from "./trpc";

// biome-ignore lint/suspicious/noExplicitAny: generic component constraint
type SurfaceProps<C extends ComponentType<any>> = Omit<
	ComponentProps<C>,
	"onClose"
>;

// biome-ignore lint/suspicious/noExplicitAny: generic component constraint
type SurfaceArgs<C extends ComponentType<any>> =
	keyof SurfaceProps<C> extends never
		? []
		: Partial<SurfaceProps<C>> extends SurfaceProps<C>
			? [props?: SurfaceProps<C>]
			: [props: SurfaceProps<C>];

// biome-ignore lint/suspicious/noExplicitAny: accepts any component
type AnyComponent = ComponentType<any>;

export const surface = {
	open<C extends AnyComponent>(component: C, ...args: SurfaceArgs<C>) {
		const [props] = args;

		trpc.ui.present.mutate({
			name: component.name,
			...(props ? { props: props as Record<string, unknown> } : {}),
		});
	},

	close() {
		trpc.ui.dismiss.mutate();
	},
};
