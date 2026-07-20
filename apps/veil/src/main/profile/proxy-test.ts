import { net } from "electron";

const TEST_URL = "https://httpbin.org/ip";
const TEST_TIMEOUT_MS = 10_000;

export type ProxyTestResult =
	| { success: true; ip: string }
	| { success: false; error: string };

export function testProxyConnection(
	testSession: Electron.Session,
	credentials?: { username: string; password: string },
): Promise<ProxyTestResult> {
	return new Promise((resolve) => {
		const timeout = setTimeout(() => {
			request.abort();
			resolve({ success: false, error: "Connection timed out" });
		}, TEST_TIMEOUT_MS);

		const request = net.request({ url: TEST_URL, session: testSession });

		if (credentials) {
			request.on("login", (_authInfo, callback) => {
				callback(credentials.username, credentials.password);
			});
		}

		request.on("response", (response) => {
			let body = "";

			response.on("data", (chunk) => {
				body += chunk.toString();
			});

			response.on("end", () => {
				clearTimeout(timeout);

				if (response.statusCode !== 200) {
					resolve({ success: false, error: `HTTP ${response.statusCode}` });

					return;
				}

				try {
					const data = JSON.parse(body) as { origin?: string };
					resolve({ success: true, ip: data.origin ?? "Unknown" });
				} catch {
					resolve({ success: false, error: "Invalid response" });
				}
			});
		});

		request.on("error", (error) => {
			clearTimeout(timeout);
			resolve({ success: false, error: error.message });
		});

		request.end();
	});
}
