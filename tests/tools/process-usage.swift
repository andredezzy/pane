// Prints, as JSON, what the kernel has counted for each given process since
// it started (proc_pid_rusage): CPU time in nanoseconds, idle and interrupt
// wakeups, the kernel's estimate of the energy it spent on the CPU in
// nanojoules, and instructions retired. A process that has ended is left
// out. Usage: process-usage <pid>...
import Darwin
import Foundation

var timebase = mach_timebase_info_data_t()
mach_timebase_info(&timebase)

func nanoseconds(_ ticks: UInt64) -> UInt64 {
	ticks * UInt64(timebase.numer) / UInt64(timebase.denom)
}

var processes: [[String: Any]] = []

for pid in CommandLine.arguments.dropFirst().compactMap({ Int32($0) }) {
	var info = rusage_info_v6()

	let read = withUnsafeMutablePointer(to: &info) { pointer in
		pointer.withMemoryRebound(to: rusage_info_t?.self, capacity: 1) {
			proc_pid_rusage(pid, RUSAGE_INFO_V6, $0)
		}
	}

	guard read == 0 else {
		continue
	}

	processes.append([
		"pid": pid,
		"cpuNs": nanoseconds(info.ri_user_time + info.ri_system_time),
		"idleWakeups": info.ri_pkg_idle_wkups,
		"interruptWakeups": info.ri_interrupt_wkups,
		"energyNj": info.ri_energy_nj,
		"instructions": info.ri_instructions,
	])
}

let output: [String: Any] = ["at": Date().timeIntervalSince1970 * 1000, "processes": processes]
let data = try JSONSerialization.data(withJSONObject: output, options: [.sortedKeys])
print(String(decoding: data, as: UTF8.self))
