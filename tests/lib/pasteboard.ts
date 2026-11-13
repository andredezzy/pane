// The system's clipboard, which a page's copy commands write: read it, and
// put back what was there before a scenario wrote it. The Mac's through
// tools/pasteboard.swift; Windows' through .NET's clipboard in PowerShell,
// every format that is text or bytes kept, as the Mac keeps every type.
import { join } from "node:path";
import { bySystem, powershell } from "./platform";
import { swiftTool } from "./tools";

async function pasteboard(...args: string[]): Promise<string> {
	return Bun.$`${await swiftTool("pasteboard")} ${args}`.text();
}

// Windows' clipboard, in a PowerShell that has it (STA, Windows Forms).
function windowsClipboard(
	script: string,
	env: Record<string, string> = {},
): Promise<string> {
	return powershell(
		`Add-Type -AssemblyName System.Windows.Forms; ${script}`,
		env,
	);
}

export const clipboardText = bySystem({
	mac: () => pasteboard("text"),
	windows: () =>
		windowsClipboard(
			"[Console]::OutputEncoding = [Text.Encoding]::UTF8; [Console]::Write([Windows.Forms.Clipboard]::GetText())",
		),
});

export const clearClipboard = bySystem({
	mac: async () => {
		await pasteboard("clear");
	},
	windows: async () => {
		await windowsClipboard("[Windows.Forms.Clipboard]::Clear()");
	},
});

// Every format of Windows' clipboard that is text or bytes, written to
// $env:PANE_FILE as JSON, and put back from it.
const SAVE_WINDOWS_CLIPBOARD = `
$saved = @()
$data = [Windows.Forms.Clipboard]::GetDataObject()
if ($data) {
	foreach ($format in $data.GetFormats($false)) {
		try { $value = $data.GetData($format, $false) } catch { continue }
		if ($value -is [string]) {
			$saved += @{ format = $format; text = $value }
		} elseif ($value -is [IO.MemoryStream]) {
			$saved += @{ format = $format; bytes = [Convert]::ToBase64String($value.ToArray()) }
		}
	}
}
[IO.File]::WriteAllText($env:PANE_FILE, (ConvertTo-Json -Compress -Depth 3 -InputObject @($saved)))
`;

const RESTORE_WINDOWS_CLIPBOARD = `
$saved = ConvertFrom-Json ([IO.File]::ReadAllText($env:PANE_FILE))
$data = New-Object Windows.Forms.DataObject
$any = $false
foreach ($entry in @($saved)) {
	if ($null -ne $entry.text) {
		$data.SetData($entry.format, $false, [string]$entry.text)
		$any = $true
	} elseif ($null -ne $entry.bytes) {
		$data.SetData($entry.format, $false, (New-Object IO.MemoryStream (, [Convert]::FromBase64String($entry.bytes))))
		$any = $true
	}
}
if ($any) { [Windows.Forms.Clipboard]::SetDataObject($data, $true) } else { [Windows.Forms.Clipboard]::Clear() }
`;

// Saves the clipboard into `directory` (a scenario's own, removed when it
// ends) and returns what puts it back.
export async function saveClipboard(
	directory: string,
): Promise<() => Promise<void>> {
	const file = join(directory, "clipboard.json");

	return bySystem({
		mac: async () => {
			await pasteboard("save", file);

			return async () => {
				await pasteboard("restore", file);
			};
		},
		windows: async () => {
			await windowsClipboard(SAVE_WINDOWS_CLIPBOARD, { PANE_FILE: file });

			return async () => {
				await windowsClipboard(RESTORE_WINDOWS_CLIPBOARD, {
					PANE_FILE: file,
				});
			};
		},
	})();
}
