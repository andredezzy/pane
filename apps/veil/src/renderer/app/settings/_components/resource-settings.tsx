import { Button } from "@pane/ui/components/button";

interface PresetOption {
	label: string;
	value: number | null;
}

// Presets rather than a free-form field: the scheduler ticks once a minute, so
// arbitrary precision would be false; Off doubles as the kill switch.
export const TAB_SLEEP_OPTIONS: PresetOption[] = [
	{ label: "5 min", value: 5 },
	{ label: "15 min", value: 15 },
	{ label: "1 hour", value: 60 },
	{ label: "Off", value: null },
];

export const PROFILE_UNLOAD_OPTIONS: PresetOption[] = [
	{ label: "15 min", value: 15 },
	{ label: "30 min", value: 30 },
	{ label: "2 hours", value: 120 },
	{ label: "Off", value: null },
];

// Megabytes. Partition isolation means no cache is ever shared, so a budget
// that looks generous per profile multiplies by every profile that exists.
export const CACHE_BUDGET_OPTIONS: PresetOption[] = [
	{ label: "100 MB", value: 100 },
	{ label: "300 MB", value: 300 },
	{ label: "1 GB", value: 1024 },
	{ label: "Off", value: null },
];

interface PresetRowProps {
	label: string;
	options: PresetOption[];
	value: number | null;
	onChange: (value: number | null) => void;
}

export function PresetRow({ label, options, value, onChange }: PresetRowProps) {
	return (
		<div>
			<span className="font-medium text-[11px]">{label}</span>

			<div className="mt-2 flex gap-1">
				{options.map((option) => (
					<Button
						key={option.label}
						type="button"
						variant={value === option.value ? "default" : "outline"}
						className="h-8 flex-1 text-[11px]"
						onClick={() => onChange(option.value)}
					>
						{option.label}
					</Button>
				))}
			</div>
		</div>
	);
}
