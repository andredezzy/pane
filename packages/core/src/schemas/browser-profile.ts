import { z } from "zod/v4";

import { Fingerprint } from "./fingerprint.js";
import { ProxyConfig } from "./proxy.js";

export const BrowserProfile = z.object({
	id: z.string(),
	name: z.string().min(1),
	group: z.string().optional(),
	notes: z.string().optional(),

	fingerprint: Fingerprint,
	proxy: ProxyConfig.optional(),

	tags: z.array(z.string()).default([]),

	createdAt: z.iso.datetime(),
	updatedAt: z.iso.datetime(),
});

export type BrowserProfileData = z.infer<typeof BrowserProfile>;
