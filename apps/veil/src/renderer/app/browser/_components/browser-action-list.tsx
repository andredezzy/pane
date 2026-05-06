import { useEffect, useRef } from "react";

declare module "react" {
	namespace JSX {
		interface IntrinsicElements {
			"browser-action-list": React.DetailedHTMLProps<
				React.HTMLAttributes<HTMLElement> & {
					partition?: string;
					tab?: string;
				},
				HTMLElement
			>;
		}
	}
}

export function BrowserActionList({ partition }: { partition: string }) {
	const ref = useRef<HTMLElement>(null);

	useEffect(() => {
		const el = ref.current;

		if (!el) {
			return;
		}

		el.setAttribute("partition", partition);
	}, [partition]);

	return <browser-action-list ref={ref} />;
}
