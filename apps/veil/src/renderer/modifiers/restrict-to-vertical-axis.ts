import { Modifier } from "@dnd-kit/abstract";
import type { DragDropManager } from "@dnd-kit/dom";

export class RestrictToVerticalAxis extends Modifier<DragDropManager> {
	apply({ transform }: { transform: { x: number; y: number } }) {
		return { x: 0, y: transform.y };
	}
}
