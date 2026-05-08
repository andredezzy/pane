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

const navigationButtonClassName =
	"flex h-[26px] w-[26px] items-center justify-center rounded text-[#71717a] transition-colors hover:bg-accent hover:text-accent-foreground";

export function ToolbarNavigationBack({
	className,
	...props
}: ButtonHTMLAttributes<HTMLButtonElement>) {
	return (
		<button
			type="button"
			className={cn(navigationButtonClassName, className)}
			{...props}
		>
			<ArrowLeft className="h-3.5 w-3.5" />
		</button>
	);
}

export function ToolbarNavigationForward({
	className,
	...props
}: ButtonHTMLAttributes<HTMLButtonElement>) {
	return (
		<button
			type="button"
			className={cn(navigationButtonClassName, className)}
			{...props}
		>
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
		<button
			type="button"
			className={cn(navigationButtonClassName, className)}
			{...props}
		>
			{isLoading ? <X className="h-3 w-3" /> : <RotateCw className="h-3 w-3" />}
		</button>
	);
}

enum ProgressPhase {
	IDLE = "IDLE",
	STARTING = "STARTING",
	GROWING = "GROWING",
	COMPLETING = "COMPLETING",
}

function getProgressWidth(phase: ProgressPhase): string {
	if (phase === ProgressPhase.STARTING) {
		return "0%";
	}

	if (phase === ProgressPhase.GROWING) {
		return "90%";
	}

	return "100%";
}

function getProgressTransition(phase: ProgressPhase): string {
	if (phase === ProgressPhase.GROWING) {
		return "width 8s cubic-bezier(0.1, 0.05, 0, 1)";
	}

	if (phase === ProgressPhase.COMPLETING) {
		return "width 200ms ease-out, opacity 200ms ease-out 200ms";
	}

	return "none";
}

function ToolbarAddressProgress({ isLoading }: { isLoading: boolean }) {
	const [phase, setPhase] = useState<ProgressPhase>(ProgressPhase.IDLE);

	useEffect(() => {
		if (isLoading && phase === ProgressPhase.IDLE) {
			setPhase(ProgressPhase.STARTING);
		} else if (
			!isLoading &&
			(phase === ProgressPhase.GROWING || phase === ProgressPhase.STARTING)
		) {
			setPhase(ProgressPhase.COMPLETING);
		}
	}, [isLoading, phase]);

	useEffect(() => {
		if (phase === ProgressPhase.STARTING) {
			const frame = requestAnimationFrame(() => {
				requestAnimationFrame(() => {
					setPhase(ProgressPhase.GROWING);
				});
			});

			return () => cancelAnimationFrame(frame);
		}
	}, [phase]);

	if (phase === ProgressPhase.IDLE) {
		return null;
	}

	return (
		<div
			className="absolute bottom-0 left-0 h-[2px] bg-gradient-to-r from-blue-500 to-blue-400"
			style={{
				width: getProgressWidth(phase),
				opacity: phase === ProgressPhase.COMPLETING ? 0 : 1,
				transition: getProgressTransition(phase),
			}}
			onTransitionEnd={(e) => {
				if (
					e.propertyName === "opacity" &&
					phase === ProgressPhase.COMPLETING
				) {
					setPhase(ProgressPhase.IDLE);
				}
			}}
		/>
	);
}

export const ToolbarAddress = forwardRef<
	HTMLInputElement,
	InputHTMLAttributes<HTMLInputElement> & { isLoading?: boolean }
>(({ className, isLoading, onFocus, onBlur, ...props }, ref) => (
	<div className="relative flex flex-1 overflow-hidden rounded-[5px]">
		<input
			ref={ref}
			data-address-bar
			type="text"
			className={cn(
				"h-[30px] flex-1 rounded-[5px] px-2.5 text-foreground text-xs transition-colors placeholder:text-muted-foreground hover:bg-[rgba(255,255,255,0.03)] focus:outline-none focus:ring-1 focus:ring-ring",
				className,
			)}
			onFocus={(e) => {
				e.currentTarget.select();
				onFocus?.(e);
			}}
			onBlur={(e) => {
				window.getSelection()?.removeAllRanges();
				onBlur?.(e);
			}}
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
