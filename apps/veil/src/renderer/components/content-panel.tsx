import { cn } from "@pane/ui/cn";
import type { HTMLAttributes } from "react";

export function ContentPanel({
	className,
	style,
	...props
}: HTMLAttributes<HTMLDivElement>) {
	return (
		<div
			className={cn(
				"m-2 ml-0 flex flex-1 flex-col overflow-hidden rounded-[10px] bg-card shadow-panel",
				className,
			)}
			style={{ WebkitAppRegion: "no-drag", ...style } as React.CSSProperties}
			{...props}
		/>
	);
}
