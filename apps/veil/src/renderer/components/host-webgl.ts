export interface HostWebgl {
	vendor: string;
	renderer: string;
}

// Read the machine's real GPU strings from a throwaway WebGL context in the app's
// own (unspoofed) renderer. A fingerprint can only swap these two strings — it
// can't fake the GPU's actual extensions, shader precision, or rendered output — so
// a profile claiming a GPU the host doesn't have is a contradiction Cloudflare
// Turnstile flags ("WebGL renderer info is spoofed"), enough to loop the challenge.
// Defaulting a host-OS profile to the true GPU keeps the claim consistent with what
// the hardware actually produces.
export function readHostWebgl(): HostWebgl | null {
	const canvas = document.createElement("canvas");

	const gl =
		canvas.getContext("webgl") ??
		(canvas.getContext("experimental-webgl") as WebGLRenderingContext | null);

	if (!gl) {
		return null;
	}

	const debugInfo = gl.getExtension("WEBGL_debug_renderer_info");

	if (!debugInfo) {
		return null;
	}

	const vendor = gl.getParameter(debugInfo.UNMASKED_VENDOR_WEBGL);
	const renderer = gl.getParameter(debugInfo.UNMASKED_RENDERER_WEBGL);

	if (typeof vendor !== "string" || typeof renderer !== "string") {
		return null;
	}

	return { vendor, renderer };
}
