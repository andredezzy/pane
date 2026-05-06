import { cn } from "@pane/ui/cn";
import type { HTMLAttributes } from "react";

export function ContentPanel({
	className,
	...props
}: HTMLAttributes<HTMLDivElement>) {
	return (
		<div
			className={cn(
				"m-2 ml-0 flex flex-1 flex-col overflow-hidden rounded-[10px] bg-card shadow-[0_0_0_1px_rgba(255,255,255,0.06),0_4px_24px_rgba(0,0,0,0.4)]",
				className,
			)}
			style={{ WebkitAppRegion: "no-drag" } as React.CSSProperties}
			{...props}
		/>
	);
}
