import { cn } from "@pane/ui/cn";
import { useEffect, useState } from "react";

const DIGITS = [
	"1",
	"2",
	"3",
	"4",
	"5",
	"6",
	"7",
	"8",
	"9",
	"",
	"0",
	"backspace",
] as const;

function BackspaceIcon() {
	return (
		<svg
			width="22"
			height="18"
			viewBox="0 0 24 20"
			fill="none"
			stroke="currentColor"
			strokeWidth={1.2}
			strokeLinecap="round"
			strokeLinejoin="round"
			role="img"
			aria-label="Backspace"
		>
			<path d="M9 2H20a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H9l-7-8 7-8z" />
			<line x1="16" y1="7" x2="12" y2="13" />
			<line x1="12" y1="7" x2="16" y2="13" />
		</svg>
	);
}

export function NumpadDots({
	length,
	filled,
	shake,
}: {
	length: number;
	filled: number;
	shake: boolean;
}) {
	return (
		<div className={cn("flex gap-3.5", shake && "animate-shake")}>
			{Array.from({ length }, (_, index) => (
				<div
					// biome-ignore lint/suspicious/noArrayIndexKey: static dot count, never reorders
					key={index}
					className={cn(
						"h-[13px] w-[13px] rounded-full transition-all duration-150",
						index < filled
							? "bg-foreground"
							: "border-[1.5px] border-foreground/25",
					)}
				/>
			))}
		</div>
	);
}

export function Numpad({
	onDigit,
	onBackspace,
}: {
	onDigit: (digit: string) => void;
	onBackspace: () => void;
}) {
	const [pressedKey, setPressedKey] = useState<string | null>(null);

	useEffect(() => {
		const handleKeyDown = (e: KeyboardEvent) => {
			if (e.key >= "0" && e.key <= "9") {
				setPressedKey(e.key);
				onDigit(e.key);
			} else if (e.key === "Backspace") {
				setPressedKey("backspace");
				onBackspace();
			}
		};

		const handleKeyUp = () => {
			setPressedKey(null);
		};

		window.addEventListener("keydown", handleKeyDown);
		window.addEventListener("keyup", handleKeyUp);

		return () => {
			window.removeEventListener("keydown", handleKeyDown);
			window.removeEventListener("keyup", handleKeyUp);
		};
	}, [onDigit, onBackspace]);

	return (
		<div className="grid grid-cols-3 gap-3.5">
			{DIGITS.map((key) => {
				if (key === "") {
					return <div key="empty" className="h-16 w-16" />;
				}

				if (key === "backspace") {
					return (
						<button
							key="backspace"
							type="button"
							className={cn(
								"flex h-16 w-16 items-center justify-center rounded-full text-foreground/35 transition-colors hover:text-foreground/60 active:text-foreground/80",
								pressedKey === "backspace" && "text-foreground/80",
							)}
							onClick={onBackspace}
						>
							<BackspaceIcon />
						</button>
					);
				}

				return (
					<button
						key={key}
						type="button"
						className={cn(
							"flex h-16 w-16 items-center justify-center rounded-full border border-foreground/12 font-extralight text-[26px] text-foreground/85 transition-colors hover:border-foreground/20 hover:text-foreground active:bg-foreground/10",
							pressedKey === key &&
								"border-foreground/20 bg-foreground/10 text-foreground",
						)}
						onClick={() => onDigit(key)}
					>
						{key}
					</button>
				);
			})}
		</div>
	);
}
