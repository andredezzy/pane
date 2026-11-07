# Chrome makes no request of its own but the extension updater's, and its CRLSet ships with the app

Every request Chrome made on its own, outside the pages, is off: some carried a profile's Google cookies, some went out from the browser itself, past every profile's proxy, with the Mac's real address, and the Electron app made none. They first showed with the per-profile proxy (ADR 0037): Chrome's own background requests to `accounts.google.com` and `www.google.com` went out per profile, through its proxy, and whether to turn them off went to the fingerprint work, which owns what a profile reveals (ADR 0038). A net log of a quiet session then named each source by its traffic annotation.

What is off:

- **The component updater** (`update_client`), with `--disable-component-update`. The on-device AI model's manifest registers itself on demand regardless, and the updater refuses it (`component_updater_service.cc`), so the hunk that made it honor the switch went in the review of 0.18.0: `quiet` and `files-inventory` still hold.
- **The network time check** (`network_time_component`), with the `NetworkTimeServiceQuerying` feature.
- **AI Mode's eligibility check** (`aim_eligibility_fetch`), which carried a profile's Google cookies and country, with the `Aim*` features.
- **The Translate ranker model** (`ranker_url_fetcher`), with the `TranslateRanker*` features. Translate itself is off per profile (`translate.enabled`).
- **The Google account list** (`gaia_auth_list_accounts`), which sent a profile's Google cookies on every launch, from sign-in metrics among other callers: patch 0008 makes `TriggerListAccounts` do nothing when Pane hosts the engine (ADR 0010), and browser sign-in is off per profile (`signin.allowed`). Signing in to Google's websites is unaffected, as the `google` and `extension-management` scenarios show.
- **The preconnect to the default search engine** (`PreconnectToSearch`): a net log of the `crlset` scenario showed connections to `www.google.com` that no page asked for, at start and again while a page showed. No request went out, so the requests in the log never named it.

The switch and the features are set in `engine/pane/browser/browser_application.mm`, the per-profile prefs in `engine/pane/browser/browser_context.mm`. The component updater, the network time check, and the on-device AI model were the requests that went out from the browser itself.

The one request Chrome still makes of its own is the extension updater's: as each profile starts, and every five hours or so after, it asks Chrome's update service (`update.googleapis.com`) for newer versions of its Web Store extensions, as every browser does (ADR 0062).

The CRLSet ships with the app:

- **Pane gives up Chrome's component updates**, as the Electron app never had them, so what they kept current comes with each build. The Chrome Root Store is built into the engine. The CRLSet was not, until 2026-10-30: with the updater off, the engine had Chromium's built-in one, which is empty (`net/cert/crl_set.cc`, `BuiltinCRLSet`), and blocked no revoked certificate beyond the static blocklist. The research comparing Pane with Aside and Dia found it: both keep Chrome's CRLSet (`CertificateRevocation/`), fetched by the component updater every few hours.
- **Each build carries Chrome's current CRLSet**, and the engine hands it to the certificate verifier as it starts, before any profile loads: on 2026-10-30, the certificates revoked by 204 issuers, 177 blocked keys, and 9 blocked interception roots (sequence 10872). Pane blocks the certificates Chrome blocks, up to the build's date.
- **`engine/scripts/fetch-crlset.sh` fetches it on the build machine** as Chrome's component updater does. Google's update service names the CRX; the script checks its SHA-256, and its CRX3 signature by the key Chrome pins for CRLSets (`crl_set_component_installer.cc`). `package.sh` puts its `crl-set` in `Contents/Resources`, keeping a day's copy in `app/build` for builds without the network, and `dist.sh` refuses a release without one. `//pane` loads it in `BrowserMainExtraParts::PreProfileInit` (`content::GetCertVerifierServiceFactory()->UpdateCRLSet`), as the component installer would.
- **A CRLSet only adds what fails**: revocations and blocked keys. Past its `NotAfter` (about four days after Google makes it), Chrome still blocks everything in it and only stops treating a certificate it covers as known good (`revocation_checker.cc`). A shipped CRLSet is as fresh as the release, which Pane's cadence (ADR 0005) keeps to days.

What was rejected:

- **Chrome's component updater for the CRLSet alone:** a request of the browser's own to Google, through no profile's proxy.
- **Fetching the CRLSet at launch from the app:** the same request, made by the app.
- **Trusting the CRLSet's download over TLS alone:** the build checks the signature Chrome checks.

How it is checked:

- **The `quiet` scenario** keeps every source off: a quiet session reaches nothing but its local fixtures, and no host but theirs is looked up, preconnected, or connected to.
- **fetch-crlset.sh** refuses a CRX with one byte changed, and one signed by any other key.
- **The `crlset` scenario** finds the CRLSet in a packaged app's bundle, and the sequence the certificate verifier used for a page (its net log) is that CRLSet's, not the built-in 0.
