// Anchored, ccTLD-aware match for Google/YouTube cookie domains. Anchoring on
// (start | dot) keeps unrelated hosts like notgoogle.com from matching, while the
// optional second label covers the regional domains Google seeds auth cookies on
// (google.co.uk, youtube.de, google.com.au, ...). Without a Public Suffix List this
// can't reject a hypothetical literal "google.<word>.<tld>" host, but every real
// Google ccTLD suffix is 2-3 letters or co.xx / com.xx, so the bound holds.
const GOOGLE_COOKIE_DOMAIN_PATTERN =
	/(?:^|\.)(?:google|youtube)\.[a-z]{2,3}(?:\.[a-z]{2,3})?$/i;

export function isGoogleCookieDomain(domain: string | undefined): boolean {
	return !!domain && GOOGLE_COOKIE_DOMAIN_PATTERN.test(domain);
}

// Matches a Google/YouTube page URL on any subdomain (and ccTLD), used to reload a
// profile's open Google tabs after a sign-out so they reflect the signed-out state.
// The subdomain class excludes / ? # so a URL delimiter can't smuggle a foreign host
// into the subdomain slot: https://evil.com#.google.com parses to host evil.com but
// would match a bare [^/]* — and this gates the post-auth continue= redirect.
const GOOGLE_URL_PATTERN =
	/^https?:\/\/(?:[^/?#]*\.)?(?:google|youtube)\.[a-z]{2,3}(?:\.[a-z]{2,3})?(?::\d+)?(?:[/?#]|$)/i;

export function isGoogleUrl(url: string): boolean {
	return GOOGLE_URL_PATTERN.test(url);
}
