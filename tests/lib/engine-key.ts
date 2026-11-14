// A profile's engine key, as Pane derives it (pane/model/data_key.h, ADR 0121), from
// the sealed data key under the mock keychain's secret: for a scenario to look
// for the key where it must not be. Only the mock keychain's key opens this way,
// and on Windows, with a PIN, only with the share the computer's TPM gives back
// for that PIN (pane/model/pin_guard.h, U5); in the test VM, the user's own
// secret too (userSecret), for its check of the user's launch.
import {
	createDecipheriv,
	createHash,
	hkdfSync,
	pbkdf2Sync,
} from "node:crypto";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { bySystem, powershell, powershellSync } from "./platform";

const MOCK_SECRET = Buffer.from("Pane mock keychain");

type Guard = { key: string; share: string };

type Sealed = {
	box: string;
	pin: { salt: string; rounds: number } | null;
	tpm?: Guard;
};

// The share a key the TPM holds gives back with `pin`, as Pane's own
// unseals it (pin_guard_win.cc): the Microsoft Platform Crypto Provider, the
// key by its name, the PIN its own, RSA-OAEP with SHA-256. The right PIN
// counts nothing in the chip's lockout.
const TPM_UNSEAL = `Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class PaneTpm {
  [DllImport("ncrypt.dll", CharSet = CharSet.Unicode)]
  static extern int NCryptOpenStorageProvider(out IntPtr provider, string name, int flags);
  [DllImport("ncrypt.dll", CharSet = CharSet.Unicode)]
  static extern int NCryptOpenKey(IntPtr provider, out IntPtr key, string name, int legacySpec, int flags);
  [DllImport("ncrypt.dll", CharSet = CharSet.Unicode)]
  static extern int NCryptSetProperty(IntPtr handle, string property, byte[] value, int size, int flags);
  [DllImport("ncrypt.dll")]
  static extern int NCryptDecrypt(IntPtr key, byte[] input, int inputSize, IntPtr padding, byte[] output, int outputSize, out int written, int flags);
  [DllImport("ncrypt.dll")]
  static extern int NCryptFreeObject(IntPtr handle);

  public static string Unseal(string name, string sealedShare, string pin) {
    IntPtr provider;
    IntPtr key;
    int status = NCryptOpenStorageProvider(out provider, "Microsoft Platform Crypto Provider", 0);
    if (status != 0) throw new Exception("provider " + status.ToString("X8"));
    status = NCryptOpenKey(provider, out key, name, 0, 0x40);
    if (status != 0) throw new Exception("key " + status.ToString("X8"));
    byte[] value = Encoding.Unicode.GetBytes(pin + "\\0");
    status = NCryptSetProperty(key, "SmartCardPin", value, value.Length, 0);
    if (status != 0) throw new Exception("pin " + status.ToString("X8"));
    IntPtr padding = Marshal.AllocHGlobal(IntPtr.Size * 2 + 4);
    Marshal.WriteIntPtr(padding, 0, Marshal.StringToHGlobalUni("SHA256"));
    Marshal.WriteIntPtr(padding, IntPtr.Size, IntPtr.Zero);
    Marshal.WriteInt32(padding, IntPtr.Size * 2, 0);
    byte[] input = Convert.FromBase64String(sealedShare);
    byte[] output = new byte[input.Length];
    int written;
    status = NCryptDecrypt(key, input, input.Length, padding, output, output.Length, out written, 0x4 | 0x40);
    NCryptFreeObject(key);
    NCryptFreeObject(provider);
    if (status != 0) throw new Exception("decrypt " + status.ToString("X8"));
    byte[] share = new byte[written];
    Array.Copy(output, share, written);
    return Convert.ToBase64String(share);
  }
}
'@
[PaneTpm]::Unseal($env:PANE_TPM_KEY, $env:PANE_TPM_SHARE, $env:PANE_TPM_PIN)`;

const unsealShare = bySystem({
	mac: (_guard: Guard, _pin: string): Buffer => {
		throw new Error("the Mac's seals take no TPM (ADR 0113)");
	},
	windows: (guard: Guard, pin: string): Buffer =>
		Buffer.from(
			powershellSync(TPM_UNSEAL, {
				PANE_TPM_KEY: guard.key,
				PANE_TPM_SHARE: guard.share,
				PANE_TPM_PIN: pin,
			}).trim(),
			"base64",
		),
});

// This user's keys in the Microsoft Platform Crypto Provider, by name.
const TPM_KEYS = `Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class PaneTpmKeys {
  [DllImport("ncrypt.dll", CharSet = CharSet.Unicode)]
  static extern int NCryptOpenStorageProvider(out IntPtr provider, string name, int flags);
  [DllImport("ncrypt.dll", CharSet = CharSet.Unicode)]
  static extern int NCryptEnumKeys(IntPtr provider, string scope, out IntPtr keyName, ref IntPtr state, int flags);
  [DllImport("ncrypt.dll")]
  static extern int NCryptFreeBuffer(IntPtr buffer);
  [DllImport("ncrypt.dll")]
  static extern int NCryptFreeObject(IntPtr handle);

  public static string Names() {
    IntPtr provider;
    if (NCryptOpenStorageProvider(out provider, "Microsoft Platform Crypto Provider", 0) != 0) return "";
    IntPtr state = IntPtr.Zero;
    IntPtr item;
    var names = new System.Text.StringBuilder();
    while (NCryptEnumKeys(provider, null, out item, ref state, 0x40) == 0) {
      names.AppendLine(Marshal.PtrToStringUni(Marshal.ReadIntPtr(item)));
      NCryptFreeBuffer(item);
    }
    if (state != IntPtr.Zero) NCryptFreeBuffer(state);
    NCryptFreeObject(provider);
    return names.ToString();
  }
}
'@
[PaneTpmKeys]::Names()`;

// The keys the TPM holds for the PIN of the data in `dataDir`, by name, as
// pin_guard_win.cc names them ("Pane data key <8 bytes of the folder's
// SHA-256> <8 random bytes>"): what the panic wipe deletes. None on the Mac,
// whose Keychain guards its secret (ADR 0113).
export const pinGuardKeys = bySystem({
	mac: async (_dataDir: string): Promise<string[]> => [],
	windows: async (dataDir: string): Promise<string[]> => {
		const folder = createHash("sha256")
			.update(dataDir.toLowerCase())
			.digest("hex")
			.slice(0, 16);

		return (await powershell(TPM_KEYS))
			.split(/\r?\n/)
			.filter((name) => name.startsWith(`Pane data key ${folder} `));
	},
});

// The secret the user's own launch seals the data key under on Windows:
// os_crypt's key in the engine folder's Local State under DPAPI ("DPAPI" and
// the blob, in base64; pane/model/engine_secret.h), which any process of the
// user's opens, as this does. The Mac's is the Keychain's.
export const userSecret = bySystem({
	mac: (_engineFolder: string): Buffer => {
		throw new Error(
			"the Mac's secret is the Keychain's, which this never reads",
		);
	},
	windows: (engineFolder: string): Buffer => {
		const sealed = Buffer.from(
			(
				JSON.parse(readFileSync(join(engineFolder, "Local State"), "utf8")) as {
					os_crypt: { encrypted_key: string };
				}
			).os_crypt.encrypted_key,
			"base64",
		);

		return Buffer.from(
			powershellSync(
				`Add-Type -AssemblyName System.Security
$blob = [Convert]::FromBase64String($env:PANE_BLOB)
[Convert]::ToBase64String([Security.Cryptography.ProtectedData]::Unprotect($blob, $null, 'CurrentUser'))`,
				{ PANE_BLOB: sealed.subarray("DPAPI".length).toString("base64") },
			).trim(),
			"base64",
		);
	},
});

function dataKey(
	dataDir: string,
	pin: string | null,
	secret: Buffer = MOCK_SECRET,
): Buffer {
	const sealed = (
		JSON.parse(readFileSync(join(dataDir, "security.json"), "utf8")) as {
			state: { key: Sealed };
		}
	).state.key;

	const salt =
		pin && sealed.pin
			? pbkdf2Sync(
					pin,
					Buffer.from(sealed.pin.salt, "base64"),
					sealed.pin.rounds,
					32,
					"sha256",
				)
			: Buffer.alloc(0);

	// A guarded seal takes the secret and the TPM's share, under its own info.
	const sealing = Buffer.from(
		pin && sealed.tpm
			? hkdfSync(
					"sha256",
					Buffer.concat([secret, unsealShare(sealed.tpm, pin)]),
					salt,
					"Pane data key, its PIN guarded",
					32,
				)
			: hkdfSync("sha256", secret, salt, "Pane data key", 32),
	);

	const box = Buffer.from(sealed.box, "base64");

	const decipher = createDecipheriv(
		"aes-256-gcm",
		sealing,
		box.subarray(0, 12),
	);

	decipher.setAuthTag(box.subarray(-16));

	return Buffer.concat([
		decipher.update(box.subarray(12, -16)),
		decipher.final(),
	]);
}

export function engineKey(
	dataDir: string,
	profile: string,
	pin: string | null,
	secret?: Buffer,
): Buffer {
	return Buffer.from(
		hkdfSync(
			"sha256",
			dataKey(dataDir, pin, secret),
			Buffer.from(profile),
			"Pane engine files",
			32,
		),
	);
}

// Each form a key could be found in, by what it is.
export function keyForms(key: Buffer): Buffer[] {
	return [
		key,
		Buffer.from(key.toString("hex")),
		Buffer.from(key.toString("base64")),
		createHash("sha256").update(key).digest(),
	];
}

// What the volume's unlocked with, as pane/model/data_key.h derives it (ADR 0121).
export function volumePassphrase(dataDir: string, pin: string | null): string {
	return Buffer.from(
		hkdfSync(
			"sha256",
			dataKey(dataDir, pin),
			Buffer.alloc(0),
			"Pane data volume, files encrypted",
			32,
		),
	).toString("base64");
}
