import bcrypt from "bcryptjs";
import { z } from "zod/v4";

import type { SecurityState } from "../../../stores/security-store";
import { MAX_ATTEMPTS } from "../../../stores/security-store";
import { executeWipe } from "../../security/wipe";
import { type Context, procedure, router } from "../trpc";

const SALT_ROUNDS = 10;

function getSecurityState(ctx: Context) {
	return ctx.stores["security-store"].getState() as SecurityState;
}

async function verifyPin(state: SecurityState, pin: string): Promise<boolean> {
	if (!state.pin) {
		return false;
	}

	return bcrypt.compare(pin, state.pin.hash);
}

export const securityRouter = router({
	verify: procedure
		.input(z.object({ pin: z.string() }))
		.mutation(async ({ input, ctx }) => {
			const state = getSecurityState(ctx);

			if (!state.pin) {
				return { success: false, remaining: 0 };
			}

			if (await verifyPin(state, input.pin)) {
				state.resetAttempts();

				return { success: true, remaining: MAX_ATTEMPTS };
			}

			const attempts = state.recordFailedAttempt();

			return { success: false, remaining: MAX_ATTEMPTS - attempts };
		}),

	setPin: procedure
		.input(z.object({ pin: z.string().min(1) }))
		.mutation(async ({ input, ctx }) => {
			const hash = await bcrypt.hash(input.pin, SALT_ROUNDS);
			const state = getSecurityState(ctx);

			state.setPin({ hash, length: input.pin.length });
			state.dismissPinScreen();
		}),

	changePin: procedure
		.input(z.object({ currentPin: z.string(), newPin: z.string().min(1) }))
		.mutation(async ({ input, ctx }) => {
			const state = getSecurityState(ctx);

			if (!(await verifyPin(state, input.currentPin))) {
				return { success: false };
			}

			const hash = await bcrypt.hash(input.newPin, SALT_ROUNDS);
			state.setPin({ hash, length: input.newPin.length });
			state.dismissPinScreen();

			return { success: true };
		}),

	removePin: procedure
		.input(z.object({ currentPin: z.string() }))
		.mutation(async ({ input, ctx }) => {
			const state = getSecurityState(ctx);

			if (!(await verifyPin(state, input.currentPin))) {
				return { success: false };
			}

			state.clearPin();
			state.dismissPinScreen();

			return { success: true };
		}),

	wipe: procedure.mutation(() => {
		executeWipe();
	}),
});
