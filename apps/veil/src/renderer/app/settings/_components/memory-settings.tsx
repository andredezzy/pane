import { Button } from "@pane/ui/components/button";

interface SleepTimerOption {
	label: string;
	minutes: number | null;
}

// Presets rather than a free-form field: the scheduler ticks once a minute, so
// arbitrary precision would be false; Off doubles as the kill switch.
export const TAB_SLEEP_OPTIONS: SleepTimerOption[] = [
	{ label: "5 min", minutes: 5 },
	{ label: "15 min", minutes: 15 },
	{ label: "1 hour", minutes: 60 },
	{ label: "Off", minutes: null },
];

export const PROFILE_UNLOAD_OPTIONS: SleepTimerOption[] = [
	{ label: "15 min", minutes: 15 },
	{ label: "30 min", minutes: 30 },
	{ label: "2 hours", minutes: 120 },
	{ label: "Off", minutes: null },
];

interface SleepTimerRowProps {
	label: string;
	options: SleepTimerOption[];
	value: number | null;
	onChange: (minutes: number | null) => void;
}

export function SleepTimerRow({
	label,
	options,
	value,
	onChange,
}: SleepTimerRowProps) {
	return (
		<div>
			<span className="font-medium text-[11px]">{label}</span>

			<div className="mt-2 flex gap-1">
				{options.map((option) => (
					<Button
						key={option.label}
						type="button"
						variant={value === option.minutes ? "default" : "outline"}
						className="h-8 flex-1 text-[11px]"
						onClick={() => onChange(option.minutes)}
					>
						{option.label}
					</Button>
				))}
			</div>
		</div>
	);
}
