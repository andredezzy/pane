// What a program of the user's own, with no entitlement, can reach of another
// process's memory: `task-probe <pid>` prints one JSON line. tests/scenarios/memory-guarded.ts.
import Darwin
import Foundation

@_silgen_name("task_read_for_pid")
func taskReadForPid(_ task: mach_port_name_t, _ pid: Int32, _ out: UnsafeMutablePointer<mach_port_name_t>) -> kern_return_t

let pid = Int32(CommandLine.arguments[1])!
var control: mach_port_name_t = 0
var read: mach_port_name_t = 0
let controlResult = task_for_pid(mach_task_self_, pid, &control)
let readResult = taskReadForPid(mach_task_self_, pid, &read)
print("{\"taskForPid\":\(controlResult),\"taskReadForPid\":\(readResult),\"control\":\(control),\"read\":\(read)}")
