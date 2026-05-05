import * as SheetPrimitive from "@radix-ui/react-dialog";
import type {
	ComponentPropsWithoutRef,
	CSSProperties,
	ElementRef,
	HTMLAttributes,
} from "react";
import { forwardRef } from "react";

import { cn } from "../cn";

const Sheet = SheetPrimitive.Root;
const SheetTrigger = SheetPrimitive.Trigger;
const SheetClose = SheetPrimitive.Close;
const SheetPortal = SheetPrimitive.Portal;

const SheetOverlay = forwardRef<
	ElementRef<typeof SheetPrimitive.Overlay>,
	ComponentPropsWithoutRef<typeof SheetPrimitive.Overlay>
>(({ className, ...props }, ref) => (
	<SheetPrimitive.Overlay
		ref={ref}
		className={cn(
			"data-[state=closed]:fade-out-0 data-[state=open]:fade-in-0 fixed inset-0 z-50 bg-black/40 data-[state=closed]:animate-out data-[state=open]:animate-in",
			className,
		)}
		style={{ WebkitAppRegion: "no-drag" } as CSSProperties}
		{...props}
	/>
));

SheetOverlay.displayName = SheetPrimitive.Overlay.displayName;

const SheetContent = forwardRef<
	ElementRef<typeof SheetPrimitive.Content>,
	ComponentPropsWithoutRef<typeof SheetPrimitive.Content>
>(({ className, children, ...props }, ref) => (
	<SheetPortal>
		<SheetOverlay />
		<SheetPrimitive.Content
			ref={ref}
			className={cn(
				"data-[state=closed]:fade-out-0 data-[state=open]:fade-in-0 data-[state=closed]:slide-out-to-right data-[state=open]:slide-in-from-right fixed top-2 right-2 bottom-2 z-50 flex w-[360px] flex-col rounded-[10px] bg-card duration-200 data-[state=closed]:animate-out data-[state=open]:animate-in",
				className,
			)}
			{...props}
		>
			{children}
		</SheetPrimitive.Content>
	</SheetPortal>
));

SheetContent.displayName = SheetPrimitive.Content.displayName;

const SheetHeader = ({
	className,
	...props
}: HTMLAttributes<HTMLDivElement>) => (
	<div
		className={cn(
			"flex items-center justify-between border-[rgba(255,255,255,0.05)] border-b px-5 py-4",
			className,
		)}
		{...props}
	/>
);

SheetHeader.displayName = "SheetHeader";

const SheetTitle = forwardRef<
	ElementRef<typeof SheetPrimitive.Title>,
	ComponentPropsWithoutRef<typeof SheetPrimitive.Title>
>(({ className, ...props }, ref) => (
	<SheetPrimitive.Title
		ref={ref}
		className={cn("font-medium text-accent-foreground text-sm", className)}
		{...props}
	/>
));

SheetTitle.displayName = SheetPrimitive.Title.displayName;

const SheetDescription = forwardRef<
	ElementRef<typeof SheetPrimitive.Description>,
	ComponentPropsWithoutRef<typeof SheetPrimitive.Description>
>(({ className, ...props }, ref) => (
	<SheetPrimitive.Description
		ref={ref}
		className={cn("text-muted-foreground text-sm", className)}
		{...props}
	/>
));

SheetDescription.displayName = SheetPrimitive.Description.displayName;

const SheetBody = ({ className, ...props }: HTMLAttributes<HTMLDivElement>) => (
	<div className={cn("flex-1 overflow-auto px-5 py-5", className)} {...props} />
);

SheetBody.displayName = "SheetBody";

const SheetFooter = ({
	className,
	...props
}: HTMLAttributes<HTMLDivElement>) => (
	<div
		className={cn(
			"flex justify-end gap-2 border-[rgba(255,255,255,0.05)] border-t px-5 py-4",
			className,
		)}
		{...props}
	/>
);

SheetFooter.displayName = "SheetFooter";

export {
	Sheet,
	SheetBody,
	SheetClose,
	SheetContent,
	SheetDescription,
	SheetFooter,
	SheetHeader,
	SheetOverlay,
	SheetPortal,
	SheetTitle,
	SheetTrigger,
};
