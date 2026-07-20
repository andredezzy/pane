import type { StoreApi } from "zustand/vanilla";

import type { UpdateState } from "../../../stores/update-store";
import { checkForUpdate, downloadUpdate } from "../../updater";
import { procedure, router } from "../trpc";

export const updatesRouter = router({
	check: procedure.mutation(({ ctx }) =>
		checkForUpdate(ctx.stores["update-store"] as StoreApi<UpdateState>),
	),

	download: procedure.mutation(({ ctx }) => {
		downloadUpdate(ctx.stores["update-store"] as StoreApi<UpdateState>);
	}),
});
