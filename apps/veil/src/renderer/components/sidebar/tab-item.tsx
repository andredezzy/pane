import { cn } from "@pane/ui/cn";
import { Globe } from "lucide-react";
import type React from "react";
import type { HTMLAttributes, ImgHTMLAttributes } from "react";

interface TabItemProps extends React.ComponentPropsWithRef<"button"> {
	active?: boolean;
}

export function TabItem({ className, active, ref, ...props }: TabItemProps) {
	return (
		<button
			ref={ref}
			type="button"
			className={cn(
				"group flex w-full items-center gap-1.5 rounded-[5px] px-2 py-1 text-xs",
				active
					? "bg-[rgba(255,255,255,0.05)] text-[#e4e4e7]"
					: "text-[#71717a] hover:bg-accent hover:text-accent-foreground",
				className,
			)}
			{...props}
		/>
	);
}

export function TabFavicon({
	src,
	className,
	...props
}: ImgHTMLAttributes<HTMLImageElement>) {
	if (!src) {
		return <Globe className={cn("h-3 w-3 shrink-0", className)} />;
	}

	return (
		<img
			src={src}
			alt=""
			className={cn("h-3 w-3 shrink-0 rounded-sm", className)}
			{...props}
		/>
	);
}

export function TabTitle({
	className,
	...props
}: HTMLAttributes<HTMLSpanElement>) {
	return (
		<span className={cn("flex-1 truncate text-left", className)} {...props} />
	);
}

export function TabNew({
	className,
	...props
}: HTMLAttributes<HTMLButtonElement>) {
	return (
		<button
			type="button"
			className={cn(
				"mt-1 flex w-full items-center gap-1.5 rounded-[5px] px-2 py-1 text-[#71717a] text-xs transition-colors hover:bg-accent hover:text-accent-foreground",
				className,
			)}
			{...props}
		>
			<span className="text-[10px]">+</span> New tab
		</button>
	);
}
