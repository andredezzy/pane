#!/usr/bin/env bash
# Fetches Chrome's current CRLSet, the list of revoked certificates and
# blocked keys Chrome's component updater keeps fresh, as that updater does:
# Google's update service names the CRX, which must match its SHA-256 and
# carry a valid signature by the key Chrome pins for CRLSets
# (chrome/browser/component_updater/crl_set_component_installer.cc). Writes
# its crl-set file to <dest>; package.sh puts it in the bundle, and the engine
# loads it at start (ADR 0111). On the build machine, never on a user's Mac.
# Usage: fetch-crlset.sh <dest>
set -euo pipefail

DEST="${1:?usage: fetch-crlset.sh <dest>}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# The CRLSet component's id, and the SHA-256 of the key that signs it.
ID="hfnkpimlhhgieaddgfemjhofmfblmnib"
KEY_SHA256="75daf8cb77684033654c97e5c51bcd817b1eeb112ce1a4338cf5725eedb84397"

curl -fsS --retry 3 -o "$work/answer.xml" \
	"https://clients2.google.com/service/update2/crx?acceptformat=crx3&prodversion=$(tr -d '[:space:]' < "$(dirname "$0")/../CHROMIUM_VERSION")&x=id%3D$ID%26v%3D%26uc"

# It names the CRX by an http URL as often as by an https one; the download
# goes over https either way, and its SHA-256 and signature are checked.
codebase="$(grep -o 'codebase="[^"]*"' "$work/answer.xml" | head -1 | cut -d'"' -f2 || true)"
sha256="$(grep -o 'hash_sha256="[0-9a-f]*"' "$work/answer.xml" | cut -d'"' -f2 || true)"
codebase="${codebase:+https://${codebase#*://}}"

if [ -z "$codebase" ] || [ -z "$sha256" ]; then
	echo "error: the update service named no CRLSet: $(cat "$work/answer.xml")" >&2
	exit 1
fi

curl -fsS --retry 3 -o "$work/crl-set.crx3" "$codebase"
echo "$sha256  $work/crl-set.crx3" | shasum -a 256 -c --quiet

python3 -I - "$work" "$KEY_SHA256" "$DEST" <<'PY'
import hashlib, io, json, pathlib, struct, subprocess, sys, zipfile

work, pinned, dest = pathlib.Path(sys.argv[1]), sys.argv[2], pathlib.Path(sys.argv[3])
crx = (work / "crl-set.crx3").read_bytes()

if crx[:4] != b"Cr24" or struct.unpack("<I", crx[4:8])[0] != 3:
    sys.exit("error: not a CRX3")

size = struct.unpack("<I", crx[8:12])[0]
header, archive = crx[12:12 + size], crx[12 + size:]


def varint(buffer, at):
    value = shift = 0
    while True:
        byte = buffer[at]
        at += 1
        value |= (byte & 0x7F) << shift
        shift += 7
        if not byte & 0x80:
            return value, at


# The protobuf fields of a CrxFileHeader (components/crx_file/crx3.proto):
# its length-delimited ones, by number.
def fields(buffer):
    at = 0
    while at < len(buffer):
        key, at = varint(buffer, at)
        if key & 7 != 2:
            sys.exit(f"error: unexpected wire type {key & 7}")
        length, at = varint(buffer, at)
        yield key >> 3, buffer[at:at + length]
        at += length


proofs, signed_header = [], None
for number, value in fields(header):
    if number == 2:  # sha256_with_rsa: an AsymmetricKeyProof
        proof = dict(fields(value))
        proofs.append((proof.get(1, b""), proof.get(2, b"")))
    elif number == 10000:
        signed_header = value

key, signature = next(((k, s) for k, s in proofs if hashlib.sha256(k).hexdigest() == pinned), (None, None))
if key is None or signed_header is None:
    sys.exit("error: the CRX is not signed by the CRLSet key")

(work / "key.der").write_bytes(key)
(work / "signature").write_bytes(signature)
(work / "signed").write_bytes(b"CRX3 SignedData\x00" + struct.pack("<I", len(signed_header)) + signed_header + archive)
subprocess.run(["openssl", "pkey", "-pubin", "-inform", "DER", "-in", work / "key.der", "-out", work / "key.pem"], check=True)
verified = subprocess.run(["openssl", "dgst", "-sha256", "-verify", work / "key.pem", "-signature", work / "signature", work / "signed"], capture_output=True)
if verified.returncode != 0:
    sys.exit("error: the CRLSet's signature does not verify")

crl_set = zipfile.ZipFile(io.BytesIO(archive)).read("crl-set")
length = struct.unpack("<H", crl_set[:2])[0]
fields_ = json.loads(crl_set[2:2 + length])
if fields_.get("ContentType") != "CRLSet":
    sys.exit("error: the crl-set file is no CRLSet")

dest.write_bytes(crl_set)
print(f"CRLSet {fields_['Sequence']}: {fields_['NumParents']} issuers, {len(fields_.get('BlockedSPKIs') or [])} blocked keys, {len(crl_set)} bytes")
PY
