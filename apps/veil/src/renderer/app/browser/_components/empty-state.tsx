export function EmptyState() {
	return (
		<div className="flex flex-1 items-center justify-center">
			<div className="flex flex-col items-center gap-3">
				<svg
					width="40"
					height="40"
					viewBox="0 0 24 24"
					fill="none"
					xmlns="http://www.w3.org/2000/svg"
					role="img"
					aria-label="Pane"
				>
					<rect
						x="3"
						y="3"
						width="18"
						height="18"
						rx="4"
						stroke="white"
						strokeWidth="1.5"
					/>
					<line
						x1="12"
						y1="3"
						x2="12"
						y2="21"
						stroke="white"
						strokeWidth="1.5"
					/>
				</svg>
				<p className="text-[#71717a] text-xs">
					Open a profile to start browsing
				</p>
			</div>
		</div>
	);
}
