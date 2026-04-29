import { z } from "zod/v4";

export const ProxyConfig = z.object({
	type: z.enum(["http", "https", "socks4", "socks5"]),
	host: z.string().min(1),
	port: z.number().int().min(1).max(65535),
	username: z.string().optional(),
	password: z.string().optional(),
});

export type ProxyConfigData = z.infer<typeof ProxyConfig>;
