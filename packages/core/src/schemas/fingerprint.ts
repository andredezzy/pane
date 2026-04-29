import { z } from "zod/v4";

const ScreenResolution = z.object({
	width: z.number().int().positive(),
	height: z.number().int().positive(),
});

const WebGLConfig = z.object({
	vendor: z.string(),
	renderer: z.string(),
});

export const Fingerprint = z.object({
	userAgent: z.string(),
	platform: z.enum(["windows", "macos", "linux"]),
	screen: ScreenResolution,
	language: z.string().default("en-US"),
	languages: z.array(z.string()).default(["en-US"]),
	timezone: z.string().default("America/New_York"),

	webgl: WebGLConfig.optional(),
	canvas: z.object({ noise: z.boolean().default(true) }).optional(),
	audioContext: z.object({ noise: z.boolean().default(true) }).optional(),

	webrtc: z
		.object({
			enabled: z.boolean().default(false),
			publicIp: z.string().optional(),
		})
		.optional(),

	hardwareConcurrency: z.number().int().min(1).max(32).default(4),
	deviceMemory: z.number().default(8),
	maxTouchPoints: z.number().int().default(0),
});

export type FingerprintData = z.infer<typeof Fingerprint>;
