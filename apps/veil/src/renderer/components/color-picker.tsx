import { cn } from "@pane/ui/cn";
import {
	PROFILE_COLOR_HEX,
	ProfileColor,
} from "../../constants/profile-colors";

interface ColorPickerProps {
	value: ProfileColor;
	onChange: (color: ProfileColor) => void;
}

const COLORS = Object.values(ProfileColor);

export function ColorPicker({ value, onChange }: ColorPickerProps) {
	return (
		<div className="flex flex-wrap gap-1.5">
			{COLORS.map((color) => {
				const hex = PROFILE_COLOR_HEX[color];
				const selected = value === color;

				return (
					<button
						key={color}
						type="button"
						onClick={() => onChange(color)}
						className={cn("h-6 w-6 rounded-md transition-shadow")}
						style={{
							background: hex,
							...(selected
								? {
										boxShadow: `0 0 0 2px var(--color-background), 0 0 0 3px ${hex}`,
									}
								: {}),
						}}
					/>
				);
			})}
		</div>
	);
}
