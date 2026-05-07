import { cn } from "@pane/ui/cn";
import { ArrowDown, ArrowUp, X } from "lucide-react";
import {
	type KeyboardEvent,
	useCallback,
	useEffect,
	useRef,
	useState,
} from "react";

import type { FindResult } from "../../../../constants/find-result";
import { trpc } from "../../../trpc";

export function FindBar({ onClose }: { onClose: () => void }) {
	const inputRef = useRef<HTMLInputElement>(null);
	const [query, setQuery] = useState("");
	const [activeMatch, setActiveMatch] = useState(0);
	const [matchCount, setMatchCount] = useState(0);

	useEffect(() => {
		inputRef.current?.focus();

		return () => {
			trpc.tabs.stopFind.mutate();
		};
	}, []);

	useEffect(() => {
		const subscription = trpc.tabs.findResults.subscribe(undefined, {
			onData(data: FindResult) {
				setActiveMatch(data.activeMatchOrdinal);
				setMatchCount(data.matches);
			},
		});

		return () => subscription.unsubscribe();
	}, []);

	const queryRef = useRef(query);
	queryRef.current = query;

	const handleChange = useCallback(
		(event: React.ChangeEvent<HTMLInputElement>) => {
			const value = event.target.value;
			setQuery(value);

			if (value.length > 0) {
				trpc.tabs.find.mutate({ text: value });
			} else {
				trpc.tabs.stopFind.mutate();
				setActiveMatch(0);
				setMatchCount(0);
			}
		},
		[],
	);

	const close = useCallback(() => {
		trpc.tabs.stopFind.mutate();
		onClose();
	}, [onClose]);

	const findNext = useCallback(() => {
		if (queryRef.current.length > 0) {
			trpc.tabs.find.mutate({
				text: queryRef.current,
				forward: true,
				findNext: true,
			});
		}
	}, []);

	const findPrevious = useCallback(() => {
		if (queryRef.current.length > 0) {
			trpc.tabs.find.mutate({
				text: queryRef.current,
				forward: false,
				findNext: true,
			});
		}
	}, []);

	const handleKeyDown = useCallback(
		(event: KeyboardEvent<HTMLInputElement>) => {
			if (event.key === "Escape") {
				close();
			} else if (event.key === "Enter" && event.shiftKey) {
				event.preventDefault();
				findPrevious();
			} else if (event.key === "Enter") {
				event.preventDefault();
				findNext();
			}
		},
		[close, findNext, findPrevious],
	);

	const hasQuery = query.length > 0;

	return (
		<div
			role="dialog"
			className="fixed inset-0 flex justify-end p-6 pt-16"
			onMouseDown={close}
		>
			<search
				className="flex h-fit items-center gap-1 rounded-lg border border-border bg-background px-2 py-1 shadow-md"
				onMouseDown={(event) => event.stopPropagation()}
			>
				<input
					ref={inputRef}
					type="text"
					value={query}
					onChange={handleChange}
					onKeyDown={handleKeyDown}
					placeholder="Find in page"
					className="h-6 w-44 bg-transparent text-foreground text-xs outline-none placeholder:text-muted-foreground"
				/>

				{hasQuery && (
					<span
						className={cn(
							"shrink-0 text-[11px] tabular-nums",
							matchCount === 0 ? "text-destructive" : "text-muted-foreground",
						)}
					>
						{activeMatch}/{matchCount}
					</span>
				)}

				<button
					type="button"
					onClick={findPrevious}
					disabled={!hasQuery || matchCount === 0}
					className="flex h-5 w-5 items-center justify-center rounded text-muted-foreground transition-colors hover:bg-accent hover:text-accent-foreground disabled:pointer-events-none disabled:opacity-30"
				>
					<ArrowUp className="h-3 w-3" />
				</button>

				<button
					type="button"
					onClick={findNext}
					disabled={!hasQuery || matchCount === 0}
					className="flex h-5 w-5 items-center justify-center rounded text-muted-foreground transition-colors hover:bg-accent hover:text-accent-foreground disabled:pointer-events-none disabled:opacity-30"
				>
					<ArrowDown className="h-3 w-3" />
				</button>

				<button
					type="button"
					onClick={close}
					className="flex h-5 w-5 items-center justify-center rounded text-muted-foreground transition-colors hover:bg-accent hover:text-accent-foreground"
				>
					<X className="h-3 w-3" />
				</button>
			</search>
		</div>
	);
}
