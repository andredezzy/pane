import Foundation

// data-lock <data folder>: whether the data's lock (Pane.lock, ADR 0113) is
// held, by flock(2), as Pane and its watchdog take it: tried without waiting,
// and let go of as this exits. Prints "held", "free", or "missing", and makes
// nothing.
let path = URL(fileURLWithPath: CommandLine.arguments[1]).appendingPathComponent("Pane.lock").path
let descriptor = open(path, O_RDWR | O_CLOEXEC)

if descriptor < 0 {
    print("missing")
} else {
    print(flock(descriptor, LOCK_EX | LOCK_NB) == 0 ? "free" : "held")
}
