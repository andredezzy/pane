import { ArrowLeft } from "lucide-react";
import { useCallback, useEffect, useRef, useState } from "react";
import { useStore } from "zustand/react";

import {
	MAX_ATTEMPTS,
	PinScreenMode,
	securityStore,
} from "../../../stores/security-store";
import { Numpad, NumpadDots } from "../../components/numpad";
import { trpc } from "../../trpc";

const noDrag = { WebkitAppRegion: "no-drag" } as React.CSSProperties;

enum Step {
	VERIFY = "VERIFY",
	ENTER = "ENTER",
	CONFIRM = "CONFIRM",
}

function getInitialStep(mode: PinScreenMode): Step {
	switch (mode) {
		case PinScreenMode.UNLOCK:
		case PinScreenMode.REMOVE:
		case PinScreenMode.CHANGE:
			return Step.VERIFY;
		case PinScreenMode.SETUP:
			return Step.ENTER;
	}
}

function getTitle(mode: PinScreenMode, step: Step): string | null {
	if (mode === PinScreenMode.UNLOCK) {
		return null;
	}

	switch (step) {
		case Step.VERIFY:
			return "Enter current PIN";
		case Step.ENTER:
			return "Enter new PIN";
		case Step.CONFIRM:
			return "Confirm PIN";
	}
}

export function PinScreen({ mode }: { mode: PinScreenMode }) {
	const pinLength = useStore(securityStore, (s) => s.pin?.length ?? 0);
	const failedAttempts = useStore(securityStore, (s) => s.failedAttempts);

	const [step, setStep] = useState(() => getInitialStep(mode));
	const [entered, setEntered] = useState("");
	const [firstPin, setFirstPin] = useState("");
	const [shake, setShake] = useState(false);
	const [checking, setChecking] = useState(false);
	const [error, setError] = useState<string | null>(null);
	const shakeTimerRef = useRef<ReturnType<typeof setTimeout>>(null);

	useEffect(() => {
		window.focus();

		return () => {
			if (shakeTimerRef.current) {
				clearTimeout(shakeTimerRef.current);
			}
		};
	}, []);

	const remaining = MAX_ATTEMPTS - failedAttempts;
	const isDismissable = mode !== PinScreenMode.UNLOCK;
	const title = getTitle(mode, step);

	const dismiss = useCallback(() => {
		securityStore.getState().dismissPinScreen();
	}, []);

	useEffect(() => {
		if (!isDismissable) {
			return;
		}

		const handle = (e: KeyboardEvent) => {
			if (e.key === "Escape") {
				dismiss();
			}
		};

		window.addEventListener("keydown", handle);

		return () => window.removeEventListener("keydown", handle);
	}, [isDismissable, dismiss]);

	let dotLength = pinLength;

	if (step === Step.ENTER) {
		dotLength = Math.max(entered.length, 4);
	} else if (step === Step.CONFIRM) {
		dotLength = firstPin.length;
	}

	let autoSubmitLength: number | null = pinLength;

	if (step === Step.ENTER) {
		autoSubmitLength = null;
	} else if (step === Step.CONFIRM) {
		autoSubmitLength = firstPin.length;
	}

	const shakeAndClear = useCallback((errorMsg?: string) => {
		if (errorMsg) {
			setError(errorMsg);
		}

		setShake(true);

		shakeTimerRef.current = setTimeout(() => {
			setShake(false);
			setEntered("");
			setChecking(false);
		}, 600);
	}, []);

	const handleVerifyResult = useCallback(
		async (pin: string) => {
			setChecking(true);

			const result = await trpc.security.verify.mutate({ pin });

			if (!result.success) {
				if (mode === PinScreenMode.UNLOCK && result.remaining <= 0) {
					await trpc.security.wipe.mutate();

					return;
				}

				shakeAndClear();

				return;
			}

			if (mode === PinScreenMode.UNLOCK) {
				securityStore.getState().unlock();

				return;
			}

			if (mode === PinScreenMode.CHANGE) {
				setEntered("");
				setStep(Step.ENTER);

				setChecking(false);
				setError(null);

				return;
			}

			if (mode === PinScreenMode.REMOVE) {
				await trpc.security.removePin.mutate({ currentPin: pin });
			}
		},
		[mode, shakeAndClear],
	);

	const handleConfirmResult = useCallback(
		async (pin: string) => {
			if (pin === firstPin) {
				await trpc.security.setPin.mutate({ pin });
			} else {
				shakeAndClear("PINs don't match");
			}
		},
		[firstPin, shakeAndClear],
	);

	const handleContinue = useCallback(() => {
		if (entered.length < 4) {
			return;
		}

		setFirstPin(entered);
		setEntered("");
		setStep(Step.CONFIRM);
		setError(null);
	}, [entered]);

	const handleDigit = useCallback(
		(digit: string) => {
			if (checking) {
				return;
			}

			const next = entered + digit;
			setEntered(next);
			setError(null);

			if (autoSubmitLength !== null && next.length === autoSubmitLength) {
				if (step === Step.VERIFY) {
					handleVerifyResult(next);
				} else if (step === Step.CONFIRM) {
					handleConfirmResult(next);
				}
			}
		},
		[
			entered,
			checking,
			autoSubmitLength,
			step,
			handleVerifyResult,
			handleConfirmResult,
		],
	);

	const handleBackspace = useCallback(() => {
		if (checking) {
			return;
		}

		setEntered((prev) => prev.slice(0, -1));
		setError(null);
	}, [checking]);

	return (
		<div className="grid h-full w-full grid-cols-[auto_1fr_auto]">
			<div className="flex items-center pl-14" style={noDrag}>
				{isDismissable && (
					<button
						type="button"
						className="text-white/30 transition-colors hover:text-white/60"
						onClick={dismiss}
					>
						<ArrowLeft className="h-5 w-5" />
					</button>
				)}
			</div>

			<div
				className="flex flex-col items-center justify-center gap-7"
				style={noDrag}
			>
				{title && (
					<p className="font-light text-[13px] text-white/50">{title}</p>
				)}

				<NumpadDots length={dotLength} filled={entered.length} shake={shake} />

				<Numpad onDigit={handleDigit} onBackspace={handleBackspace} />

				{step === Step.ENTER && entered.length >= 4 && (
					<button
						type="button"
						className="font-light text-[13px] text-white/60 hover:text-white/80"
						onClick={handleContinue}
					>
						Continue with {entered.length}-digit PIN
					</button>
				)}

				{step === Step.ENTER && entered.length < 4 && (
					<p className="font-light text-[13px] text-white/30">
						Enter at least 4 digits
					</p>
				)}

				{error && (
					<p className="font-light text-[13px] text-red-500">{error}</p>
				)}

				{mode === PinScreenMode.UNLOCK && failedAttempts > 0 && (
					<p
						className={
							remaining === 1
								? "font-light text-[13px] text-red-500"
								: "font-light text-[13px] text-white/30"
						}
					>
						{remaining === 1
							? "Last attempt. All data will be erased."
							: `${remaining} attempts remaining`}
					</p>
				)}
			</div>

			<div />
		</div>
	);
}
