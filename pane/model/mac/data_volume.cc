// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/data_volume.h"

#include <CoreFoundation/CoreFoundation.h>
#include <DiskArbitration/DiskArbitration.h>
#include <dispatch/dispatch.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <tuple>
#include <utility>

#include "base/apple/foundation_util.h"
#include "base/apple/scoped_cftyperef.h"
#include "base/apple/scoped_dispatch_object.h"
#include "base/containers/span.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/mac/mac_util.h"
#include "base/memory/ref_counted.h"
#include "base/posix/eintr_wrapper.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "base/types/expected_macros.h"
#include "base/uuid.h"
#include "base/values.h"
#include "pane/model/mac/file_operations.h"
#include "pane/model/mac/panic_wipe.h"

namespace pane::model {

namespace {

constexpr char kDiskutil[] = "/usr/sbin/diskutil";
constexpr char kHdiutil[] = "/usr/bin/hdiutil";
constexpr char kNewfsApfs[] = "/sbin/newfs_apfs";

// diskutil names an APFS container and volume by their role, hdiutil by
// their type's GUID.
constexpr std::array<std::string_view, 2> kContainerHints = {
    "Apple_APFS_Container", "EF57347C-0000-11AA-AA11-00306543ECAC"};
constexpr std::array<std::string_view, 2> kVolumeHints = {
    "Apple_APFS_Volume", "41504653-0000-11AA-AA11-00306543ECAC"};

constexpr char kEncryptedMarker[] = ".files-encrypted";
constexpr char kRepairedMarker[] = ".files-repaired";
constexpr char kOpeningMarker[] = ".safety-started";
constexpr char kFailedMarker[] = ".safety-killed";
constexpr char kRestoredMarker[] = ".safety-restored";
constexpr char kSafetyCopy[] = "SafetyCopy";
constexpr int kSafeOpens = 3;

std::string SafeOpen(int count) {
  return base::StrCat({".safety-opened-", base::NumberToString(count)});
}

// `diskutil image`, where macOS has it (26 and later), since hdiutil's
// attach is deprecated there.
bool HasDiskutilImage() {
  return base::mac::MacOSMajorVersion() >= 26;
}

std::string Device(std::string_view name) {
  return name.starts_with("/dev/") ? std::string(name)
                                   : base::StrCat({"/dev/", name});
}

// As UUID().uuidString names it, for a staging folder.
std::string NewStageId() {
  return base::ToUpperASCII(base::Uuid::GenerateRandomV4().AsLowercaseString());
}

base::unexpected<VolumeError> FileFailed(const base::FilePath& path) {
  return base::unexpected(VolumeError::File(LastFileError(path)));
}

// With its links resolved (/var is /private/var), as hdiutil may name it.
base::FilePath RealPath(const base::FilePath& path) {
  const base::FilePath resolved = base::MakeAbsoluteFilePath(path);
  return resolved.empty() ? path : resolved;
}

// The two ends of a pipe, which no process this one starts keeps but by the
// remapping asked for.
bool MakePipe(base::ScopedFD& read_end, base::ScopedFD& write_end) {
  std::array<int, 2> ends;
  if (pipe(ends.data()) != 0) {
    return false;
  }
  read_end.reset(ends[0]);
  write_end.reset(ends[1]);
  return fcntl(ends[0], F_SETFD, FD_CLOEXEC) == 0 &&
         fcntl(ends[1], F_SETFD, FD_CLOEXEC) == 0;
}

// A tool's standard output and error, read together to their ends, so
// neither fills its pipe while the other is read.
void ReadOutputs(const base::ScopedFD& out,
                 const base::ScopedFD& err,
                 std::string& output,
                 std::string& errors) {
  std::array<pollfd, 2> ends = {
      {{out.get(), POLLIN, 0}, {err.get(), POLLIN, 0}}};
  const std::array<std::string*, 2> sinks = {&output, &errors};
  std::array<char, 4096> buffer;
  size_t remaining = ends.size();
  while (remaining > 0) {
    const int ready =
        HANDLE_EINTR(poll(ends.data(), static_cast<nfds_t>(ends.size()), -1));
    if (ready < 0) {
      return;
    }
    for (size_t i = 0; i < ends.size(); ++i) {
      if (ends[i].fd < 0 || ends[i].revents == 0) {
        continue;
      }
      const ssize_t size =
          HANDLE_EINTR(read(ends[i].fd, buffer.data(), buffer.size()));
      if (size > 0) {
        sinks[i]->append(base::as_string_view(
            base::span(buffer).first(static_cast<size_t>(size))));
      } else {
        // Its end, or an error: poll passes over a negative descriptor.
        ends[i].fd = -1;
        --remaining;
      }
    }
  }
}

// A disk image tool, with `input` on its standard input (a passphrase is
// never an argument, which other processes can read), and its output.
base::expected<std::string, VolumeError> Run(
    const std::string& tool,
    const std::vector<std::string>& arguments,
    std::optional<std::string_view> input = std::nullopt) {
  std::vector<std::string> named = {tool};
  for (const std::string& argument :
       base::span(arguments).first(std::min<size_t>(2, arguments.size()))) {
    named.push_back(argument);
  }
  const std::string command = base::JoinString(named, " ");

  base::ScopedFD stdin_read, stdin_write, stdout_read, stdout_write,
      stderr_read, stderr_write;
  if (!MakePipe(stdin_read, stdin_write) ||
      !MakePipe(stdout_read, stdout_write) ||
      !MakePipe(stderr_read, stderr_write)) {
    return base::unexpected(VolumeError::Tool(command, -1, "no pipe"));
  }
  base::LaunchOptions options;
  options.fds_to_remap = {{stdin_read.get(), STDIN_FILENO},
                          {stdout_write.get(), STDOUT_FILENO},
                          {stderr_write.get(), STDERR_FILENO}};
  std::vector<std::string> argv = {tool};
  argv.insert(argv.end(), arguments.begin(), arguments.end());
  base::Process process = base::LaunchProcess(argv, options);
  stdin_read.reset();
  stdout_write.reset();
  stderr_write.reset();
  if (!process.IsValid()) {
    return base::unexpected(VolumeError::Tool(command, -1, "did not start"));
  }

  if (input) {
    // A tool that ends before it reads its input raises no SIGPIPE here: the
    // write fails, and the tool's exit status says why.
    fcntl(stdin_write.get(), F_SETNOSIGPIPE, 1);
    std::ignore = base::WriteFileDescriptor(stdin_write.get(), *input);
  }
  stdin_write.reset();

  std::string output, errors;
  ReadOutputs(stdout_read, stderr_read, output, errors);
  int status = -1;
  if (!process.WaitForExit(&status) || status != 0) {
    return base::unexpected(
        VolumeError::Tool(command, status, errors + output));
  }
  return output;
}

// A tool's property list as a value: its dictionaries, arrays, and strings,
// which are all that is read here; anything else is none.
base::Value ValueOf(CFTypeRef property) {
  if (CFDictionaryRef dictionary =
          base::apple::CFCast<CFDictionaryRef>(property)) {
    const size_t count = static_cast<size_t>(CFDictionaryGetCount(dictionary));
    std::vector<const void*> keys(count);
    std::vector<const void*> values(count);
    CFDictionaryGetKeysAndValues(dictionary, keys.data(), values.data());
    base::DictValue dict;
    for (size_t i = 0; i < count; ++i) {
      if (CFStringRef key = base::apple::CFCast<CFStringRef>(keys[i])) {
        dict.Set(base::SysCFStringRefToUTF8(key), ValueOf(values[i]));
      }
    }
    return base::Value(std::move(dict));
  }
  if (CFArrayRef array = base::apple::CFCast<CFArrayRef>(property)) {
    base::ListValue list;
    for (CFIndex i = 0; i < CFArrayGetCount(array); ++i) {
      list.Append(ValueOf(CFArrayGetValueAtIndex(array, i)));
    }
    return base::Value(std::move(list));
  }
  if (CFStringRef string = base::apple::CFCast<CFStringRef>(property)) {
    return base::Value(base::SysCFStringRefToUTF8(string));
  }
  return base::Value();
}

// The dictionary a tool's -plist output holds; nullopt for anything else.
std::optional<base::DictValue> DictionaryOf(std::string_view output) {
  const base::span<const uint8_t> bytes = base::as_byte_span(output);
  base::apple::ScopedCFTypeRef<CFDataRef> data(CFDataCreate(
      kCFAllocatorDefault, bytes.data(), static_cast<CFIndex>(bytes.size())));
  base::apple::ScopedCFTypeRef<CFPropertyListRef> plist(
      data ? CFPropertyListCreateWithData(kCFAllocatorDefault, data.get(),
                                          kCFPropertyListImmutable, nullptr,
                                          nullptr)
           : nullptr);
  if (!plist) {
    return std::nullopt;
  }
  base::Value value = ValueOf(plist.get());
  if (!value.is_dict()) {
    return std::nullopt;
  }
  return std::move(value).TakeDict();
}

// An array of dictionaries, as Swift's `as? [[String: Any]]` casts one: none
// if any of it is something else.
std::optional<std::vector<const base::DictValue*>> DictionariesIn(
    const base::Value* value) {
  const base::ListValue* list = value ? value->GetIfList() : nullptr;
  if (!list) {
    return std::nullopt;
  }
  std::vector<const base::DictValue*> dictionaries;
  for (const base::Value& item : *list) {
    const base::DictValue* dict = item.GetIfDict();
    if (!dict) {
      return std::nullopt;
    }
    dictionaries.push_back(dict);
  }
  return dictionaries;
}

std::string_view ContentHint(const base::DictValue& entity) {
  const std::string* hint = entity.FindString("content-hint");
  return hint ? std::string_view(*hint) : std::string_view();
}

bool IsContainer(std::string_view hint) {
  return std::ranges::find(kContainerHints, hint) != kContainerHints.end();
}

// The image's own device among an attachment's: its whole disk, which
// detaches it whole, not its APFS container's, a whole disk too. They come in
// no set order: a partitioned image's EFI partition came first. diskutil
// names them without /dev/, hdiutil with it.
std::optional<std::string> ImageDevice(
    const std::vector<const base::DictValue*>& entities) {
  constexpr std::string_view kWholeDisk = "/dev/disk";
  for (const base::DictValue* entity : entities) {
    const std::string* entry = entity->FindString("dev-entry");
    if (!entry) {
      continue;
    }
    const std::string device = Device(*entry);
    if (device.starts_with(kWholeDisk) &&
        std::ranges::all_of(std::string_view(device).substr(kWholeDisk.size()),
                            base::IsAsciiDigit<char>) &&
        !IsContainer(ContentHint(*entity))) {
      return device;
    }
  }
  return std::nullopt;
}

// The device of the first entity of one of `hints`.
std::optional<std::string> DeviceOf(
    const std::vector<const base::DictValue*>& entities,
    base::span<const std::string_view> hints) {
  const auto entity =
      std::ranges::find_if(entities, [&](const base::DictValue* entity) {
        return std::ranges::find(hints, ContentHint(*entity)) != hints.end();
      });
  const std::string* entry =
      entity == entities.end() ? nullptr : (*entity)->FindString("dev-entry");
  return entry ? std::make_optional(Device(*entry)) : std::nullopt;
}

// A disk just made is ready a moment after the tool that made it returns, on
// a loaded Mac: on CI, newfs_apfs found no /dev/disk5, then could not open a
// /dev/disk7 just detached and given again; diskutil found no container after
// it, no APFS volume to unlock, for over 3 s once, and DiskArbitration no
// disk to mount; each in one of a few runs.
// `body` is asked again for up to 10 s while `not_yet` takes its failure for
// that.
template <typename T>
base::expected<T, VolumeError> WhenReady(
    base::FunctionRef<base::expected<T, VolumeError>()> body,
    base::FunctionRef<bool(const VolumeError&)> not_yet) {
  const base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(10);
  while (true) {
    base::expected<T, VolumeError> result = body();
    if (result.has_value() || !not_yet(result.error()) ||
        base::TimeTicks::Now() >= deadline) {
      return result;
    }
    base::PlatformThread::Sleep(base::Milliseconds(100));
  }
}

bool AlwaysNotYet(const VolumeError&) {
  return true;
}

bool ToolSaid(const VolumeError& error, std::string_view text) {
  return error.kind == VolumeError::Kind::kTool &&
         error.output.find(text) != std::string::npos;
}

// Whether macOS reports the volume mounted at `path` encrypted.
bool VolumeIsEncrypted(const base::FilePath& path) {
  base::apple::ScopedCFTypeRef<CFURLRef> url =
      base::apple::FilePathToCFURL(path);
  base::apple::ScopedCFTypeRef<CFTypeRef> value;
  if (!url ||
      !CFURLCopyResourcePropertyForKey(url.get(), kCFURLVolumeIsEncryptedKey,
                                       value.InitializeInto(), nullptr)) {
    return false;
  }
  const CFBooleanRef encrypted = base::apple::CFCast<CFBooleanRef>(value.get());
  return encrypted && CFBooleanGetValue(encrypted);
}

// One mount's answer, which Disk Arbitration gives on its queue. The mount
// holds a reference for the callback, so an answer later than the wait finds
// it still there, where the Swift's, unretained, was gone.
class MountResult : public base::RefCountedThreadSafe<MountResult> {
 public:
  MountResult() = default;
  MountResult(const MountResult&) = delete;
  MountResult& operator=(const MountResult&) = delete;

  // Disk Arbitration's callback: `context` is the result the mount took a
  // reference to, which this lets go of.
  static void Answer(DADiskRef, DADissenterRef dissenter, void* context) {
    MountResult* result = static_cast<MountResult*>(context);
    result->status_ = dissenter ? DADissenterGetStatus(dissenter) : 0;
    result->done_.Signal();
    result->Release();
  }

  // The status Disk Arbitration answered with, 0 for mounted; nullopt with no
  // answer within `timeout`.
  std::optional<DAReturn> Wait(base::TimeDelta timeout) {
    if (!done_.TimedWait(timeout)) {
      return std::nullopt;
    }
    return status_;
  }

 private:
  friend class base::RefCountedThreadSafe<MountResult>;
  ~MountResult() = default;

  base::WaitableEvent done_;
  // Written before `done_` signals, read after.
  DAReturn status_ = 0;
};

// `link` leading to `target`, in place of anything else there.
base::expected<void, VolumeError> Link(const base::FilePath& link,
                                       const base::FilePath& target) {
  base::FilePath destination;
  if (base::ReadSymbolicLink(link, &destination) && destination == target) {
    return base::ok();
  }
  if (ItemExists(link)) {
    RETURN_IF_ERROR(RemoveItem(link), &VolumeError::File);
  }
  if (!base::CreateDirectory(link.DirName())) {
    return FileFailed(link.DirName());
  }
  if (!base::CreateSymbolicLink(target, link)) {
    return FileFailed(link);
  }
  return base::ok();
}

// The files' sizes under `folder`.
int64_t SizeOf(const base::FilePath& folder) {
  int64_t total = 0;
  base::FileEnumerator entries(folder, /*recursive=*/true,
                               base::FileEnumerator::FILES |
                                   base::FileEnumerator::DIRECTORIES |
                                   base::FileEnumerator::SHOW_SYM_LINKS);
  for (base::FilePath entry = entries.Next(); !entry.empty();
       entry = entries.Next()) {
    const base::stat_wrapper_t& info = entries.GetInfo().stat();
    if (S_ISREG(info.st_mode)) {
      total += info.st_size;
    }
  }
  return total;
}

}  // namespace

// static
VolumeError VolumeError::Tool(std::string command,
                              int status,
                              std::string output) {
  return {.kind = Kind::kTool,
          .what = std::move(command),
          .status = status,
          .output = std::move(output)};
}

// static
VolumeError VolumeError::NotEncrypted(std::string volume) {
  return {.kind = Kind::kNotEncrypted, .what = std::move(volume)};
}

// static
VolumeError VolumeError::File(std::string what) {
  return {.kind = Kind::kFile, .what = std::move(what)};
}

std::string VolumeError::ToString() const {
  switch (kind) {
    case Kind::kTool:
      return base::StrCat(
          {what, " exited with ", base::NumberToString(status), ": ", output});
    case Kind::kNotEncrypted:
      return base::StrCat({"the volume ", what, " is not encrypted"});
    case Kind::kFile:
      return what;
  }
}

struct DataVolume::Attachment {
  std::string image;
  std::optional<std::string> container;
  std::optional<std::string> volume;
};

DataVolume::DataVolume(const base::FilePath& data_directory,
                       std::optional<base::FilePath> cache_directory)
    : data_directory_(data_directory),
      cache_directory_(std::move(cache_directory)),
      image_(data_directory.Append("Data.sparsebundle")),
      mount_point_(data_directory.Append("Engine").Append("Volume")) {}

DataVolume::DataVolume(const DataVolume&) = default;
DataVolume& DataVolume::operator=(const DataVolume&) = default;
DataVolume::~DataVolume() = default;

bool DataVolume::Exists() const {
  return base::PathExists(image_);
}

bool DataVolume::IsMounted() const {
  const base::FilePath resolved = base::MakeAbsoluteFilePath(mount_point_);
  struct statfs info;
  if (resolved.empty() || statfs(resolved.value().c_str(), &info) != 0) {
    return false;
  }
  const base::span<const char> name(info.f_mntonname);
  const auto end = std::ranges::find(name, '\0');
  return base::as_string_view(name.first(
             static_cast<size_t>(end - name.begin()))) == resolved.value();
}

// Attaching

base::expected<DataVolume::Devices, VolumeError> DataVolume::AttachOrMake(
    std::string_view passphrase) const {
  RETURN_IF_ERROR(PrepareMountPoint());
  const bool made = !Exists();
  if (made) {
    RETURN_IF_ERROR(MakeImage());
  }

  base::expected<Attachment, VolumeError> attached = AttachOnce();
  if (!attached.has_value()) {
    DetachLeftovers();
    attached = AttachOnce();
  }
  RETURN_IF_ERROR(attached);
  if (attached->volume) {
    return Devices{.image = attached->image, .volume = *attached->volume};
  }

  std::string container;
  if (attached->container) {
    container = *attached->container;
  } else {
    ASSIGN_OR_RETURN(container, MakeContainer(attached->image, made));
  }
  ASSIGN_OR_RETURN(const std::string output,
                   Run(kDiskutil,
                       {"apfs", "addVolume", container, "APFS", "Pane",
                        "-stdinpassphrase", "-nomount"},
                       passphrase));

  // diskutil names the volume it made on a line of its own.
  constexpr std::string_view kPrefix = "Disk from APFS operation: ";
  std::optional<std::string> volume;
  for (std::string_view line : base::SplitStringPiece(
           output, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (line.starts_with(kPrefix)) {
      volume = std::string(base::TrimWhitespaceASCII(
          line.substr(kPrefix.size()), base::TRIM_ALL));
    }
  }
  if (!volume) {
    ASSIGN_OR_RETURN(volume, VolumeNamed("Pane", container));
  }
  return Devices{.image = attached->image, .volume = Device(*volume)};
}

// A terabyte at most, sparse, with no file system. `diskutil image create`
// where macOS has it, in 70 ms; hdiutil's takes a second.
base::expected<void, VolumeError> DataVolume::MakeImage() const {
  if (HasDiskutilImage() &&
      Run(kDiskutil, {"image", "create", "blank", "--fs", "None", "--format",
                      "UDSB", "--size", "1T", image_.value()})
          .has_value()) {
    return base::ok();
  }
  RETURN_IF_ERROR(Run(
      kHdiutil, {"create", "-size", "1t", "-type", "SPARSEBUNDLE", "-layout",
                 "NONE", image_.RemoveFinalExtension().value(), "-quiet"}));
  return base::ok();
}

// The image's APFS container, made on it unless one is there: formatting
// erases, so only an image just made, or one diskutil finds no container on,
// is formatted.
base::expected<std::string, VolumeError> DataVolume::MakeContainer(
    const std::string& image,
    bool made) const {
  if (!made) {
    if (base::expected<std::string, VolumeError> container = ContainerOf(image);
        container.has_value()) {
      return container;
    }
  }

  RETURN_IF_ERROR(WhenReady<std::string>(
      [&] { return Run(kNewfsApfs, {"-C", "-w", image}); },
      [](const VolumeError& error) {
        return ToolSaid(error, "unable to open") ||
               ToolSaid(error, "unable to format");
      }));
  return WhenReady<std::string>([&] { return ContainerOf(image); },
                                &AlwaysNotYet);
}

// Only "not found" is asked again: a wrong passphrase fails at once.
base::expected<void, VolumeError> DataVolume::Unlock(
    const std::string& volume,
    std::string_view passphrase) const {
  RETURN_IF_ERROR(WhenReady<std::string>(
      [&] {
        return Run(
            kDiskutil,
            {"apfs", "unlockVolume", volume, "-stdinpassphrase", "-nomount"},
            passphrase);
      },
      [](const VolumeError& error) {
        return ToolSaid(error, "Could not find APFS Volume");
      }));
  return base::ok();
}

base::expected<DataVolume::Devices, VolumeError> DataVolume::AttachImageOnce()
    const {
  ASSIGN_OR_RETURN(const Attachment attached, AttachOnce());
  if (!attached.volume) {
    return base::unexpected(
        VolumeError::Tool("attach", 0, "no APFS volume in the image"));
  }
  return Devices{.image = attached.image, .volume = *attached.volume};
}

// One attach, nothing cleared on failure: before the engine's singleton, a
// Pane already running may hold the image, and keeps it. `diskutil image
// attach` where macOS has it, since hdiutil's attach is deprecated there;
// hdiutil on the macOS versions before it.
base::expected<DataVolume::Attachment, VolumeError> DataVolume::AttachOnce()
    const {
  base::expected<std::string, VolumeError> output =
      base::unexpected(VolumeError::Tool(kDiskutil, 0, ""));
  if (HasDiskutilImage()) {
    output = Run(kDiskutil,
                 {"image", "attach", "--noMount", "--plist", image_.value()});
  }
  if (!output.has_value()) {
    output = Run(kHdiutil, {"attach", image_.value(), "-nomount", "-plist",
                            "-noverify", "-noautoopen"});
  }
  RETURN_IF_ERROR(output);

  const std::optional<base::DictValue> plist = DictionaryOf(*output);
  const std::optional<std::vector<const base::DictValue*>> entities =
      plist ? DictionariesIn(plist->Find("system-entities")) : std::nullopt;
  const std::optional<std::string> image =
      entities ? ImageDevice(*entities) : std::nullopt;
  if (!image) {
    return base::unexpected(VolumeError::Tool("attach", 0, *output));
  }
  return Attachment{.image = *image,
                    .container = DeviceOf(*entities, kContainerHints),
                    .volume = DeviceOf(*entities, kVolumeHints)};
}

base::expected<void, VolumeError> DataVolume::Open(
    const Devices& devices,
    std::string_view passphrase,
    std::optional<std::string_view> former) const {
  base::expected<void, VolumeError> unlocked =
      Unlock(devices.volume, passphrase);

  if (!unlocked.has_value() && former) {
    base::expected<void, VolumeError> unlocked_with_former =
        Unlock(devices.volume, *former);
    if (unlocked_with_former.has_value()) {
      unlocked = unlocked_with_former;
      // Nothing converts while an earlier Pane can still open this volume,
      // and raze what it can't read.
      RETURN_IF_ERROR(ChangePassphrase(devices, *former, passphrase));
    } else {
      // Both failed since it is left unlocked, after a crash, which takes the
      // change all the same; or it is on neither, which the mount says.
      std::ignore = ChangePassphrase(devices, *former, passphrase);
    }
  }

  if (base::expected<void, VolumeError> mounted = Mount(devices);
      !mounted.has_value()) {
    // A volume that would not unlock says why.
    RETURN_IF_ERROR(unlocked);
    return mounted;
  }

  if (!VolumeIsEncrypted(mount_point_)) {
    std::ignore = Detach(devices.image, /*force=*/true);
    return base::unexpected(VolumeError::NotEncrypted(devices.volume));
  }
  return base::ok();
}

// The volume's one user, from one passphrase to another. diskutil reads both
// from its standard input, a line each: neither is an argument.
base::expected<void, VolumeError> DataVolume::ChangePassphrase(
    const Devices& devices,
    std::string_view old_passphrase,
    std::string_view new_passphrase) const {
  RETURN_IF_ERROR(
      Run(kDiskutil,
          {"apfs", "changePassphrase", devices.volume, "-user", "disk",
           "-oldStdinpassphrase", "-newStdinpassphrase"},
          base::StrCat({old_passphrase, "\n", new_passphrase, "\n"})));
  return base::ok();
}

base::expected<void, VolumeError> DataVolume::Mount(
    const Devices& devices) const {
  if (IsMounted()) {
    return base::ok();
  }
  RETURN_IF_ERROR(PrepareMountPoint());

  base::apple::ScopedCFTypeRef<DASessionRef> session(
      DASessionCreate(kCFAllocatorDefault));
  base::apple::ScopedCFTypeRef<DADiskRef> disk(
      session ? DADiskCreateFromBSDName(kCFAllocatorDefault, session.get(),
                                        devices.volume.c_str())
              : nullptr);
  base::apple::ScopedCFTypeRef<CFURLRef> path =
      base::apple::FilePathToCFURL(mount_point_);
  if (!disk || !path) {
    return base::unexpected(VolumeError::Tool(
        "mount", 0, base::StrCat({"no disk ", devices.volume})));
  }

  base::apple::ScopedDispatchObject<dispatch_queue_t> queue(
      dispatch_queue_create("pane.volume.mount", DISPATCH_QUEUE_SERIAL));
  DASessionSetDispatchQueue(session.get(), queue.get());

  // A volume just made can reach DiskArbitration a moment after diskutil
  // unlocked it, on a loaded Mac, which then refuses it as a bad argument
  // (WhenReady); anything else fails at once.
  const base::expected<void, VolumeError> mounted = WhenReady<void>(
      [&]() -> base::expected<void, VolumeError> {
        auto result = base::MakeRefCounted<MountResult>();
        std::array<CFStringRef, 2> arguments = {CFSTR("nobrowse"), nullptr};
        // The callback's reference, which it lets go of.
        result->AddRef();
        DADiskMountWithArguments(
            disk.get(), path.get(), kDADiskMountOptionDefault,
            &MountResult::Answer, result.get(), arguments.data());
        const std::optional<DAReturn> status = result->Wait(base::Seconds(30));
        if (status != 0) {
          return base::unexpected(
              VolumeError::Tool(base::StrCat({"mount ", devices.volume}),
                                status.value_or(-1), ""));
        }
        return base::ok();
      },
      [](const VolumeError& error) {
        return error.kind == VolumeError::Kind::kTool &&
               error.status == static_cast<DAReturn>(kDAReturnBadArgument);
      });
  DASessionSetDispatchQueue(session.get(), nullptr);
  RETURN_IF_ERROR(mounted);

  // The volume's root, the user's alone.
  base::SetPosixFilePermissions(mount_point_, 0700);

  // Spotlight leaves it alone: mdutil can't tell its state on a volume out of
  // the Finder, and an index would outlive the lock (ADR 0121).
  const base::FilePath unindexed = mount_point_.Append(".metadata_never_index");
  if (!base::PathExists(unindexed)) {
    Touch(unindexed);
  }

  StopFileEventsLog();
  return base::ok();
}

// macOS logs the names of the files that change at a volume's root, in the
// clear, and an earlier Pane's logs name each site's folders: an empty
// `no_log` turns that off, and what is there goes.
void DataVolume::StopFileEventsLog() const {
  const base::FilePath events = mount_point_.Append(".fseventsd");
  const base::FilePath no_log = events.Append("no_log");
  if (base::PathExists(no_log)) {
    return;
  }

  base::CreateDirectory(events);
  Touch(no_log);

  base::FileEnumerator items(events, /*recursive=*/false,
                             base::FileEnumerator::FILES |
                                 base::FileEnumerator::DIRECTORIES |
                                 base::FileEnumerator::SHOW_SYM_LINKS);
  for (base::FilePath item = items.Next(); !item.empty(); item = items.Next()) {
    const std::string name = item.BaseName().value();
    if (name != "no_log" && name != "fseventsd-uuid") {
      base::DeletePathRecursively(item);
    }
  }
}

// The APFS container a volume is in.
base::expected<std::string, VolumeError> DataVolume::ContainerOf(
    const std::string& volume) const {
  ASSIGN_OR_RETURN(const std::string info,
                   Run(kDiskutil, {"info", "-plist", volume}));
  const std::optional<base::DictValue> plist = DictionaryOf(info);
  const std::string* container =
      plist ? plist->FindString("APFSContainerReference") : nullptr;
  if (!container) {
    return base::unexpected(VolumeError::Tool("diskutil info", 0, info));
  }
  return *container;
}

base::expected<std::string, VolumeError> DataVolume::VolumeNamed(
    std::string_view name,
    const std::string& container) const {
  ASSIGN_OR_RETURN(const std::string list,
                   Run(kDiskutil, {"apfs", "list", "-plist", container}));
  const std::optional<base::DictValue> plist = DictionaryOf(list);
  const std::optional<std::vector<const base::DictValue*>> containers =
      plist ? DictionariesIn(plist->Find("Containers")) : std::nullopt;
  const std::optional<std::vector<const base::DictValue*>> volumes =
      containers && !containers->empty()
          ? DictionariesIn(containers->front()->Find("Volumes"))
          : std::nullopt;
  const std::string* identifier = nullptr;
  if (volumes) {
    const auto named =
        std::ranges::find_if(*volumes, [&](const base::DictValue* volume) {
          const std::string* volume_name = volume->FindString("Name");
          return volume_name && *volume_name == name;
        });
    identifier = named == volumes->end()
                     ? nullptr
                     : (*named)->FindString("DeviceIdentifier");
  }
  if (!identifier) {
    return base::unexpected(VolumeError::Tool("diskutil apfs list", 0, list));
  }
  return Device(*identifier);
}

base::expected<void, VolumeError> DataVolume::Detach(
    std::optional<std::string_view> device,
    bool force) const {
  std::vector<std::string> arguments = {
      "detach", device ? std::string(*device) : mount_point_.value(), "-quiet"};
  if (force) {
    arguments.push_back("-force");
  }
  RETURN_IF_ERROR(Run(kHdiutil, arguments));
  return base::ok();
}

void DataVolume::DetachLeftovers() const {
  for (const std::string& device : DevicesHoldingImage()) {
    std::ignore = Detach(device, /*force=*/true);
  }
}

bool DataVolume::Holds(std::string_view device) const {
  return std::ranges::contains(DevicesHoldingImage(), Device(device));
}

// Each device hdiutil lists for this image.
std::vector<std::string> DataVolume::DevicesHoldingImage() const {
  const base::expected<std::string, VolumeError> output =
      Run(kHdiutil, {"info", "-plist"});
  const std::optional<base::DictValue> info =
      output.has_value() ? DictionaryOf(*output) : std::nullopt;
  const std::optional<std::vector<const base::DictValue*>> images =
      info ? DictionariesIn(info->Find("images")) : std::nullopt;
  if (!images) {
    return {};
  }

  const base::FilePath path = RealPath(image_);
  std::vector<std::string> devices;
  for (const base::DictValue* image : *images) {
    const std::string* image_path = image->FindString("image-path");
    if (!image_path || RealPath(base::FilePath(*image_path)) != path) {
      continue;
    }
    const std::optional<std::vector<const base::DictValue*>> entities =
        DictionariesIn(image->Find("system-entities"));
    if (std::optional<std::string> device =
            entities ? ImageDevice(*entities) : std::nullopt) {
      devices.push_back(std::move(*device));
    }
  }
  return devices;
}

base::expected<base::Process, VolumeError> DataVolume::Watch(
    const base::FilePath& helper) const {
  base::Process process = base::LaunchProcess(
      {helper.value(), base::NumberToString(base::GetCurrentProcId()),
       image_.value()},
      base::LaunchOptions());
  if (!process.IsValid()) {
    return base::unexpected(
        VolumeError::Tool(helper.value(), -1, "did not start"));
  }
  return process;
}

// Made once, and read-only while nothing is attached on it.
base::expected<void, VolumeError> DataVolume::PrepareMountPoint() const {
  if (!base::CreateDirectory(mount_point_)) {
    return FileFailed(mount_point_);
  }
  if (!IsMounted()) {
    base::SetPosixFilePermissions(mount_point_, 0500);
  }
  return base::ok();
}

// Profiles

base::FilePath DataVolume::Folder(std::string_view profile_id) const {
  return mount_point_.Append(profile_id);
}

bool DataVolume::HasFolder(std::string_view profile_id) const {
  return base::PathExists(Folder(profile_id));
}

base::expected<void, VolumeError> DataVolume::Prepare(
    std::string_view profile_id,
    bool saves_history) const {
  if (IsPlainFolder(EngineFolder(profile_id))) {
    RETURN_IF_ERROR(MoveIn({std::string(profile_id)}, [](double) {}));
  }

  const base::FilePath folder = Folder(profile_id);
  for (std::string_view name : {"Profile", "Cache"}) {
    if (!base::CreateDirectory(folder.Append(name))) {
      return FileFailed(folder.Append(name));
    }
  }
  RETURN_IF_ERROR(Link(EngineFolder(profile_id), folder.Append("Profile")));

  if (const std::optional<base::FilePath> caches = CacheFolder(profile_id)) {
    if (IsPlainFolder(*caches)) {
      RETURN_IF_ERROR(Stage(*caches, cache_directory_, NewStageId()));
    }
    RETURN_IF_ERROR(Link(*caches, folder.Append("Cache")));
  }

  if (!saves_history) {
    // The caches are in Cache, or beside the profile's own files where the
    // data root is not under the home's Application Support.
    for (std::string_view item :
         {"Profile/shared_proto_db", "Profile/Cache", "Profile/Code Cache",
          "Cache/Cache", "Cache/Code Cache"}) {
      base::DeletePathRecursively(folder.Append(item));
    }
  }
  return base::ok();
}

// Encrypting a profile's files (ADR 0121)

bool DataVolume::FilesAreEncrypted(std::string_view profile_id) const {
  return base::PathExists(Folder(profile_id).Append(kEncryptedMarker));
}

void DataVolume::MarkFilesEncrypted(std::string_view profile_id) const {
  Touch(Folder(profile_id).Append(kEncryptedMarker));
  MarkFilesRepaired(profile_id);
}

bool DataVolume::FilesAreRepaired(std::string_view profile_id) const {
  return base::PathExists(Folder(profile_id).Append(kRepairedMarker));
}

void DataVolume::MarkFilesRepaired(std::string_view profile_id) const {
  Touch(Folder(profile_id).Append(kRepairedMarker));
}

DataVolume::SafetyCopyStep DataVolume::SafetyCopyWillOpen(
    std::string_view profile_id) const {
  const base::FilePath folder = Folder(profile_id);
  if (!base::PathExists(folder.Append(kSafetyCopy)) ||
      base::PathExists(folder.Append(SafeOpen(1)))) {
    return SafetyCopyStep::kNone;
  }

  // 0.18.0 counted a quit as a kill, so what it left proves nothing.
  for (std::string_view marker : {".safety-opening", ".safety-failed"}) {
    base::DeletePathRecursively(folder.Append(marker));
  }

  const base::FilePath opening = folder.Append(kOpeningMarker);
  const base::FilePath failed = folder.Append(kFailedMarker);
  const base::FilePath restored = folder.Append(kRestoredMarker);
  SafetyCopyStep step = SafetyCopyStep::kNone;

  if (base::PathExists(opening)) {
    if (base::PathExists(failed)) {
      // Once: a copy that fails again would fail for good, and stays.
      if (!base::PathExists(restored)) {
        step = SafetyCopyStep::kRestore;
      }
    } else {
      Touch(failed);
    }
  }

  Touch(opening);
  return step;
}

void DataVolume::SafetyCopyDidRestore(std::string_view profile_id) const {
  const base::FilePath folder = Folder(profile_id);
  base::DeletePathRecursively(folder.Append(kEncryptedMarker));
  Touch(folder.Append(kRestoredMarker));
  base::DeletePathRecursively(folder.Append(kFailedMarker));
  base::DeletePathRecursively(folder.Append(kOpeningMarker));
}

void DataVolume::SafetyCopyDidOpen(std::string_view profile_id) const {
  const base::FilePath folder = Folder(profile_id);
  if (!base::PathExists(folder.Append(kSafetyCopy))) {
    return;
  }

  base::DeletePathRecursively(folder.Append(kOpeningMarker));
  base::DeletePathRecursively(folder.Append(kFailedMarker));

  std::vector<base::FilePath> opens;
  for (int count = 1; count <= kSafeOpens; ++count) {
    opens.push_back(folder.Append(SafeOpen(count)));
  }

  if (const auto next = std::ranges::find_if_not(
          opens,
          [](const base::FilePath& open) { return base::PathExists(open); });
      next != opens.end()) {
    Touch(*next);
  }

  if (FilesAreRepaired(profile_id) &&
      std::ranges::all_of(opens, [](const base::FilePath& open) {
        return base::PathExists(open);
      })) {
    opens.push_back(folder.Append(kRestoredMarker));
    opens.push_back(folder.Append(kSafetyCopy));
    for (const base::FilePath& item : opens) {
      base::DeletePathRecursively(item);
    }
  }
}

void DataVolume::SafetyCopiesClosed() const {
  base::FileEnumerator folders(mount_point_, /*recursive=*/false,
                               base::FileEnumerator::FILES |
                                   base::FileEnumerator::DIRECTORIES |
                                   base::FileEnumerator::SHOW_SYM_LINKS);
  for (base::FilePath folder = folders.Next(); !folder.empty();
       folder = folders.Next()) {
    base::DeletePathRecursively(folder.Append(kOpeningMarker));
  }
}

base::expected<void, VolumeError> DataVolume::MoveIn(
    const std::vector<std::string>& profile_ids,
    base::FunctionRef<void(double share)> progress) const {
  std::vector<std::string> plain;
  int64_t total = 0;
  for (const std::string& id : profile_ids) {
    if (IsPlainFolder(EngineFolder(id))) {
      plain.push_back(id);
      total += SizeOf(EngineFolder(id));
    }
  }
  const std::string move = NewStageId();
  int64_t copied = 0;

  for (const std::string& id : plain) {
    const base::FilePath target = Folder(id).Append("Profile");

    if (base::PathExists(target)) {
      RETURN_IF_ERROR(RemoveItem(target), &VolumeError::File);
    }

    if (!base::CreateDirectory(Folder(id))) {
      return FileFailed(Folder(id));
    }
    RETURN_IF_ERROR(
        CopyContents(EngineFolder(id), target,
                     [&](int64_t size) {
                       copied += size;
                       progress(total > 0
                                    ? std::min(static_cast<double>(copied) /
                                                   static_cast<double>(total),
                                               1.0)
                                    : 1.0);
                     }),
        &VolumeError::File);

    RETURN_IF_ERROR(Stage(EngineFolder(id), data_directory_, move));
    RETURN_IF_ERROR(Link(EngineFolder(id), target));

    if (const std::optional<base::FilePath> caches = CacheFolder(id);
        caches && IsPlainFolder(*caches)) {
      RETURN_IF_ERROR(Stage(*caches, cache_directory_, move));
    }
  }

  progress(1);
  return base::ok();
}

bool DataVolume::HasPlainFolder(std::string_view profile_id) const {
  return IsPlainFolder(EngineFolder(profile_id));
}

bool DataVolume::HasPlainProfiles() const {
  base::FileEnumerator entries(data_directory_.Append("Engine"),
                               /*recursive=*/false,
                               base::FileEnumerator::FILES |
                                   base::FileEnumerator::DIRECTORIES |
                                   base::FileEnumerator::SHOW_SYM_LINKS);
  for (base::FilePath entry = entries.Next(); !entry.empty();
       entry = entries.Next()) {
    const std::string name = entry.BaseName().value();
    if (base::Uuid::ParseCaseInsensitive(name).is_valid() &&
        HasPlainFolder(name)) {
      return true;
    }
  }
  return false;
}

base::expected<void, VolumeError> DataVolume::Remove(
    std::string_view profile_id) const {
  std::vector<base::FilePath> paths = {EngineFolder(profile_id)};
  if (std::optional<base::FilePath> caches = CacheFolder(profile_id)) {
    paths.push_back(std::move(*caches));
  }
  paths.push_back(Folder(profile_id));

  for (const base::FilePath& path : paths) {
    if (ItemExists(path)) {
      RETURN_IF_ERROR(RemoveItem(path), &VolumeError::File);
    }
  }
  return base::ok();
}

base::FilePath DataVolume::EngineFolder(std::string_view profile_id) const {
  return data_directory_.Append("Engine").Append(profile_id);
}

std::optional<base::FilePath> DataVolume::CacheFolder(
    std::string_view profile_id) const {
  if (!cache_directory_) {
    return std::nullopt;
  }
  return cache_directory_->Append("Engine").Append(profile_id);
}

// Into `root`'s Wiping folder, where the panic wipe erases it.
base::expected<void, VolumeError> DataVolume::Stage(
    const base::FilePath& path,
    const std::optional<base::FilePath>& root,
    std::string_view id) const {
  if (!root) {
    return base::ok();
  }
  const base::FilePath folder = root->Append(kWipeStagingName).Append(id);
  if (!base::CreateDirectory(folder)) {
    return FileFailed(folder);
  }
  RETURN_IF_ERROR(MoveItem(path, folder.Append(path.BaseName())),
                  &VolumeError::File);
  return base::ok();
}

}  // namespace pane::model
