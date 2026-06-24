// The in-tab page shown while a real Chrome window handles Google sign-in. Its
// "Transfer session to Pane" button logs __PANE_TRANSFER__, which GoogleSignIn
// listens for to pull the freshly-minted cookies back into the profile session.
export const GOOGLE_AUTH_PAGE = `data:text/html,${encodeURIComponent(`<!DOCTYPE html>
<html>
<head><meta charset="utf-8"><style>
* { margin: 0; padding: 0; box-sizing: border-box; }
body {
	font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", system-ui, sans-serif;
	display: flex; flex-direction: column; align-items: center; justify-content: center;
	min-height: 100vh; background: #0a0a0a; color: #e4e4e7;
	padding: 40px 24px; gap: 32px;
}
svg.logo { width: 40px; height: 40px; }
h1 { font-size: 28px; font-weight: 500; letter-spacing: -0.03em; color: #fafafa; }
.desc { font-size: 15px; color: #71717a; line-height: 1.6; max-width: 480px; text-align: center; }
ol {
	list-style: none; counter-reset: steps;
	display: flex; flex-direction: column; gap: 12px;
	max-width: 400px; width: 100%; margin: 8px 0;
}
li {
	counter-increment: steps; font-size: 14px; color: #a1a1aa;
	line-height: 1.5; padding-left: 32px; position: relative;
}
li::before {
	content: counter(steps); position: absolute; left: 0; top: 1px;
	width: 20px; height: 20px; border-radius: 50%;
	background: #27272a; color: #71717a; font-size: 11px;
	display: flex; align-items: center; justify-content: center;
}
button {
	padding: 14px 40px; font-size: 15px; font-weight: 500;
	border-radius: 10px; border: none; cursor: pointer;
	font-family: inherit; transition: all 0.15s ease;
	background: #fafafa; color: #09090b; margin-top: 8px;
}
button:hover { background: #e4e4e7; }
button:active { transform: scale(0.98); }
button:disabled { opacity: 0.4; cursor: not-allowed; transform: none; }
#err { color: #f87171; font-size: 13px; display: none; max-width: 400px; text-align: center; }
</style></head>
<body>
<svg class="logo" viewBox="0 0 48 48">
	<path fill="#EA4335" d="M24 9.5c3.54 0 6.71 1.22 9.21 3.6l6.85-6.85C35.9 2.38 30.47 0 24 0 14.62 0 6.51 5.38 2.56 13.22l7.98 6.19C12.43 13.72 17.74 9.5 24 9.5z"/>
	<path fill="#4285F4" d="M46.98 24.55c0-1.57-.15-3.09-.38-4.55H24v9.02h12.94c-.58 2.96-2.26 5.48-4.78 7.18l7.73 6c4.51-4.18 7.09-10.36 7.09-17.65z"/>
	<path fill="#34A853" d="M10.53 28.59A14.5 14.5 0 019.5 24c0-1.59.28-3.14.76-4.59l-7.98-6.19C.92 16.46 0 20.12 0 24c0 3.88.92 7.54 2.56 10.78l7.97-6.19z"/>
	<path fill="#FBBC05" d="M24 48c6.48 0 11.93-2.13 15.89-5.81l-7.73-6c-2.15 1.45-4.92 2.3-8.16 2.3-6.26 0-11.57-4.22-13.47-9.91l-7.98 6.19C6.51 42.62 14.62 48 24 48z"/>
</svg>
<h1>Sign in with Google Chrome</h1>
<p class="desc">Google requires signing in through a supported browser. A Chrome window has been opened for you.</p>
<ol>
	<li>Sign in to your Google account in the Chrome window</li>
	<li>Once signed in, come back to Pane</li>
	<li>Click the button below to transfer your session</li>
</ol>
<button id="btn" onclick="this.disabled=true;this.textContent='Transferring session...';console.log('__PANE_TRANSFER__')">
	Transfer session to Pane
</button>
<p id="err"></p>
</body>
</html>`)}`;
