import { memo } from "react";
import { TextMorph } from "torph/react";

import { UpdateStatus } from "../../stores/update-store";

// Memoized so progress ticks re-rendering the parent don't re-trigger the
// morph — TextMorph re-animates on every re-render.
export const UpdateStatusLabel = memo(function UpdateStatusLabel({
	status,
}: {
	status: UpdateStatus;
}) {
	let label = "Update available";

	if (status === UpdateStatus.DOWNLOADING) {
		label = "Downloading update";
	} else if (status === UpdateStatus.DOWNLOADED) {
		label = "Downloaded, quit to install";
	}

	return <TextMorph>{label}</TextMorph>;
});
