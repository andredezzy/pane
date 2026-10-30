# Chrome's CRLSet ships with the app

Each build carries Chrome's current CRLSet, and the engine hands it to the certificate verifier as it starts, before any profile loads: the certificates revoked by 204 issuers, 177 blocked keys, and 9 blocked interception roots (sequence 10872, 2026-10-30). Pane blocks the certificates Chrome blocks, up to the build's date.

ADR 0040 said "CRLSets and the Chrome Root Store update with the app", but no CRLSet shipped: with the component updater off, the engine had Chromium's built-in one, which is empty (`net/cert/crl_set.cc`, `BuiltinCRLSet`), and blocked no revoked certificate beyond the static blocklist. The research comparing Pane with Aside and Dia found it: both keep Chrome's CRLSet (`CertificateRevocation/`), fetched by the component updater every few hours.

`engine/scripts/fetch-crlset.sh` fetches it on the build machine as Chrome's component updater does. Google's update service names the CRX; the script checks its SHA-256, and its CRX3 signature by the key Chrome pins for CRLSets (`crl_set_component_installer.cc`). `package.sh` puts its `crl-set` in `Contents/Resources`, keeping a day's copy in `app/build` for builds without the network, and `dist.sh` refuses a release without one. `//pane` loads it in `BrowserMainExtraParts::PreProfileInit` (`content::GetCertVerifierServiceFactory()->UpdateCRLSet`), as the component installer would.

A CRLSet only adds what fails: revocations and blocked keys. Past its `NotAfter` (about four days after Google makes it), Chrome still blocks everything in it and only stops treating a certificate it covers as known good (`revocation_checker.cc`). A shipped CRLSet is as fresh as the release, which Pane's cadence (ADR 0108) keeps to days.

What was rejected:

- **Chrome's component updater for the CRLSet alone:** a request of the browser's own to Google, through no profile's proxy, which ADR 0040 rules out.
- **Fetching it at launch from the app:** the same request, made by the app.
- **Trusting the download over TLS alone:** the build checks the signature Chrome checks.

How it is checked:

- **fetch-crlset.sh** refuses a CRX with one byte changed, and one signed by any other key.
- **The crlset scenario** finds the CRLSet in a packaged app's bundle, and the sequence the certificate verifier used for a page (its net log) is that CRLSet's, not the built-in 0.
- **The quiet scenario** still sees no request of Pane's own.
