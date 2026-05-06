import { cn } from "@pane/ui/cn";
import { ArrowLeft, ArrowRight, RotateCw, X } from "lucide-react";
import {
	type ButtonHTMLAttributes,
	Children,
	forwardRef,
	type HTMLAttributes,
	type InputHTMLAttributes,
	type PropsWithChildren,
	useEffect,
	useState,
} from "react";
import {
	PROFILE_COLOR_HEX,
	type ProfileColor,
} from "../../../../constants/profile-colors";

export function Toolbar({
	className,
	...props
}: HTMLAttributes<HTMLDivElement>) {
	return (
		<div
			className={cn("flex items-center gap-1.5 p-1.5", className)}
			{...props}
		/>
	);
}

export function ToolbarNavigation({
	className,
	...props
}: HTMLAttributes<HTMLDivElement>) {
	return <div className={cn("flex gap-0.5", className)} {...props} />;
}

const navBtnClass =
	"flex h-[26px] w-[26px] items-center justify-center rounded text-[#71717a] transition-colors hover:bg-accent hover:text-accent-foreground";

export function ToolbarNavigationBack({
	className,
	...props
}: ButtonHTMLAttributes<HTMLButtonElement>) {
	return (
		<button type="button" className={cn(navBtnClass, className)} {...props}>
			<ArrowLeft className="h-3.5 w-3.5" />
		</button>
	);
}

export function ToolbarNavigationForward({
	className,
	...props
}: ButtonHTMLAttributes<HTMLButtonElement>) {
	return (
		<button type="button" className={cn(navBtnClass, className)} {...props}>
			<ArrowRight className="h-3.5 w-3.5" />
		</button>
	);
}

export function ToolbarNavigationReload({
	className,
	isLoading,
	...props
}: ButtonHTMLAttributes<HTMLButtonElement> & { isLoading?: boolean }) {
	return (
		<button type="button" className={cn(navBtnClass, className)} {...props}>
			{isLoading ? <X className="h-3 w-3" /> : <RotateCw className="h-3 w-3" />}
		</button>
	);
}

type ProgressPhase = "idle" | "starting" | "growing" | "completing";

function getProgressWidth(phase: ProgressPhase): string {
	if (phase === "starting") {
		return "0%";
	}

	if (phase === "growing") {
		return "90%";
	}

	return "100%";
}

function getProgressTransition(phase: ProgressPhase): string {
	if (phase === "growing") {
		return "width 8s cubic-bezier(0.1, 0.05, 0, 1)";
	}

	if (phase === "completing") {
		return "width 200ms ease-out, opacity 200ms ease-out 200ms";
	}

	return "none";
}

function ToolbarAddressProgress({ isLoading }: { isLoading: boolean }) {
	const [phase, setPhase] = useState<ProgressPhase>("idle");

	useEffect(() => {
		if (isLoading && phase === "idle") {
			setPhase("starting");
		} else if (!isLoading && (phase === "growing" || phase === "starting")) {
			setPhase("completing");
		}
	}, [isLoading, phase]);

	useEffect(() => {
		if (phase === "starting") {
			const frame = requestAnimationFrame(() => {
				requestAnimationFrame(() => {
					setPhase("growing");
				});
			});

			return () => cancelAnimationFrame(frame);
		}
	}, [phase]);

	if (phase === "idle") {
		return null;
	}

	return (
		<div
			className="absolute bottom-0 left-0 h-[2px] bg-gradient-to-r from-blue-500 to-blue-400"
			style={{
				width: getProgressWidth(phase),
				opacity: phase === "completing" ? 0 : 1,
				transition: getProgressTransition(phase),
			}}
			onTransitionEnd={(e) => {
				if (e.propertyName === "opacity" && phase === "completing") {
					setPhase("idle");
				}
			}}
		/>
	);
}

export const ToolbarAddress = forwardRef<
	HTMLInputElement,
	InputHTMLAttributes<HTMLInputElement> & { isLoading?: boolean }
>(({ className, isLoading, ...props }, ref) => (
	<div className="relative flex flex-1 overflow-hidden rounded-[5px]">
		<input
			ref={ref}
			type="text"
			className={cn(
				"h-[30px] flex-1 rounded-[5px] bg-[rgba(255,255,255,0.03)] px-2.5 text-foreground text-xs placeholder:text-muted-foreground focus:outline-none focus:ring-1 focus:ring-ring",
				className,
			)}
			{...props}
		/>
		<ToolbarAddressProgress isLoading={!!isLoading} />
	</div>
));

ToolbarAddress.displayName = "ToolbarAddress";

interface ToolbarProfileProps extends HTMLAttributes<HTMLDivElement> {
	color: ProfileColor;
}

export function ToolbarProfile({
	className,
	color,
	children,
	...props
}: ToolbarProfileProps) {
	const hex = PROFILE_COLOR_HEX[color];

	return (
		<div
			className={cn(
				"flex h-[30px] items-center rounded-[5px] px-2.5 font-medium text-[11px]",
				className,
			)}
			style={{
				background: `color-mix(in srgb, ${hex} 8%, transparent)`,
				border: `1px solid color-mix(in srgb, ${hex} 30%, transparent)`,
				color: hex,
			}}
			{...props}
		>
			{children}
		</div>
	);
}

export function ToolbarExtensions({
	className,
	children,
	...props
}: PropsWithChildren<HTMLAttributes<HTMLDivElement>>) {
	if (Children.count(children) === 0) {
		return null;
	}

	return (
		<div className={cn("flex gap-0.5", className)} {...props}>
			{children}
		</div>
	);
}
