//----------------------------------------------------------------------------
//
// TSDuck - The MPEG Transport Stream Toolkit
// Copyright (c) 2005-2023, Thierry Lelegard
// BSD-2-Clause license, see LICENSE.txt file or https://tsduck.io/#license
//
//----------------------------------------------------------------------------

#include "tsSysUtils.h"
#include "tsFileUtils.h"
#include "tsTime.h"
#include "tsArgs.h"

#if defined(TS_WINDOWS)
    #include "tsWinUtils.h"
    #include "tsBeforeStandardHeaders.h"
    #include <intrin.h>
    #include <io.h>
    #include <psapi.h>
    #include "tsAfterStandardHeaders.h"
#elif defined(TS_LINUX)
    #include "tsBeforeStandardHeaders.h"
    #include <sys/resource.h>
    #include <dlfcn.h>
    #include "tsAfterStandardHeaders.h"
#elif defined(TS_MAC)
    #include "tsBeforeStandardHeaders.h"
    #include <sys/resource.h>
    #include <mach/mach.h>
    #include <mach/message.h>
    #include <mach/kern_return.h>
    #include <mach/task_info.h>
    #include <libproc.h>
    #include <dlfcn.h>
    #include "tsAfterStandardHeaders.h"
#elif defined(TS_BSD)
    #include "tsSysCtl.h"
    #include "tsBeforeStandardHeaders.h"
    #include <sys/user.h>
    #include <sys/resource.h>
    #include <kvm.h>
    #include <signal.h>
    #include <dlfcn.h>
    #if defined(TS_FREEBSD)
        #include <libprocstat.h>
    #elif defined(TS_DRAGONFLYBSD)
        #include <sys/kinfo.h>
    #endif
    #include "tsAfterStandardHeaders.h"
#endif

// Required link libraries under Windows.
#if defined(TS_WINDOWS) && defined(TS_MSC)
    #pragma comment(lib, "psapi.lib")  // GetProcessMemoryInfo
#endif


//----------------------------------------------------------------------------
// Return the name of the current application executable file.
//----------------------------------------------------------------------------

ts::UString ts::ExecutableFile()
{
    UString path;

#if defined(TS_WINDOWS)

    // Window implementation.
    std::array<::WCHAR, 2048> name;
    ::DWORD length = ::GetModuleFileNameW(nullptr, name.data(), ::DWORD(name.size()));
    path = UString(name, length);

#elif defined(TS_LINUX)

    // Linux implementation.
    // /proc/self/exe is a symbolic link to the executable.
    path = fs::weakly_canonical("/proc/self/exe", &ErrCodeReport());

#elif defined(TS_MAC)

    // MacOS implementation.
    // The function proc_pidpath is documented as "private" and "subject to change".
    // Another option is _NSGetExecutablePath (not tested here yet).
    int length = 0;
    char name[PROC_PIDPATHINFO_MAXSIZE];
    if ((length = ::proc_pidpath(getpid(), name, sizeof(name))) < 0) {
        throw ts::Exception(u"proc_pidpath error", errno);
    }
    else {
        assert(length <= int(sizeof(name)));
        path.assignFromUTF8(name, length);
    }

#elif defined(TS_FREEBSD) || defined(TS_DRAGONFLYBSD)

    // FreeBSD and DragonFlyBSD implementation.
    // We use the sysctl() MIB and the OID for the current executable is:
    path = SysCtrlString({CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1}); // -1 means current process

#elif defined(TS_NETBSD)

    // NetBSD implementation.
    // We use the sysctl() MIB and the OID for the current executable is:
    path = SysCtrlString({CTL_KERN, KERN_PROC_ARGS, -1, KERN_PROC_PATHNAME}); // -1 means current process

#elif defined(TS_OPENBSD)

    // OpenBSD implementation.
    // OpenBSD is the only OS without supported interface to get the current executable path,
    // giving invalid so-called "security reasons" for that. So, we try to guess it from the
    // original argv[0]. This is much less secure than having a supported interface. This is
    // why their "security reasons" are particularly stupid IMHO.

    ByteBlock argv_data(SysCtrlBytes({CTL_KERN, KERN_PROC_ARGS, ::getpid(), KERN_PROC_ARGV}));
    if (argv_data.size() < sizeof(char*)) {
        return UString();
    }
    char** argv = reinterpret_cast<char**>(argv_data.data());
    char* exe = argv[0];
    if (exe == nullptr) {
        return UString();
    }
    if (::strchr(exe, '/') != nullptr) {
        // A path is provided, resolve it.
        char* path8 = ::realpath(exe, nullptr);
        if (path8 != nullptr) {
            path.assignFromUTF8(path8);
            ::free(path8);
        }
    }
    else {
        // A simple command name is provided, find it in the PATH.
        path = SearchExecutableFile(UString::FromUTF8(exe));
    }

#else
#error "ts::ExecutableFile not implemented on this system"
#endif

    return path.empty() ? path : AbsoluteFilePath(path);
}


//----------------------------------------------------------------------------
//! Get the name of the executable or shared library containing the caller.
//----------------------------------------------------------------------------

ts::UString ts::CallerLibraryFile()
{
#if defined(TSDUCK_STATIC)

    // In case of static build, there is no shared library.
    // All code is in the main executable.
    return ExecutableFile();

#elif defined(TS_MSC)

    // Window implementation.
    // Get return address of current function (in caller code).
    void* const ret = _ReturnAddress();
    // Get the module (DLL) into which this address can be found.
    ::HMODULE handle = nullptr;
    if (::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, ::LPCWSTR(ret), &handle) == 0) {
        return UString();
    }
    else {
        std::array<::WCHAR, 2048> name;
        ::DWORD length = ::GetModuleFileNameW(handle, name.data(), ::DWORD(name.size()));
        return UString(name, length);
    }

#elif defined(TS_GCC) || defined(TS_LLVM)

    // GCC and LLVM/clang implementation.
    // Get return address of current function (in caller code).
    void* const ret = __builtin_return_address(0);
    // Get the shared library into which this address can be found.
    ::Dl_info info;
    TS_ZERO(info);
    if (ret != nullptr && ::dladdr(ret, &info) != 0 && info.dli_fname != nullptr) {
        return UString::FromUTF8(info.dli_fname);
    }
    else {
        return UString();
    }

#else
    #error "ts::CallerLibraryFile not implemented on this system"
#endif
}


//----------------------------------------------------------------------------
// Get current process characteristics.
//----------------------------------------------------------------------------

ts::ProcessId ts::CurrentProcessId()
{
#if defined(TS_WINDOWS)
    return ::GetCurrentProcessId();
#else
    return ::getpid();
#endif
}

bool ts::IsPrivilegedUser()
{
#if defined(TS_WINDOWS)
    ::SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    ::PSID AdministratorsGroup;
    ::BOOL ok = ::AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &AdministratorsGroup);
    if (ok) {
        if (!::CheckTokenMembership(nullptr, AdministratorsGroup, &ok)) {
            ok = false;
        }
        ::FreeSid(AdministratorsGroup);
    }
    return ok;
#else
    return ::geteuid() == 0;
#endif
}


//----------------------------------------------------------------------------
// Format an error code into a string
//----------------------------------------------------------------------------

#if !defined(TS_WINDOWS)
// Depending on GNU vs. POSIX, strerror_r returns an int or a char*.
// There are two short functions to handle the strerror_r result.
// The C++ compiler will automatically invoke the right one.
// The other one is unused (disable unused warning).

TS_PUSH_WARNING()
TS_LLVM_NOWARNING(unused-function)
TS_GCC_NOWARNING(unused-function)
namespace {
    // POSIX version, strerror_r returns an int, leave result unmodified.
    inline void handle_strerror_r(bool& found, char*& result, int strerror_t_ret)
    {
        found = strerror_t_ret == 0; // success
    }
    // GNU version, strerror_r returns char*, not necessarily in buffer.
    inline void handle_strerror_r(bool& found, char*& result, char* strerror_t_ret)
    {
        result = strerror_t_ret; // actual message
        found = result != nullptr;
    }
}
TS_POP_WARNING()
#endif // not Windows

// Portable public interface:
ts::UString ts::SysErrorCodeMessage(ts::SysErrorCode code)
{
#if defined(TS_WINDOWS)
    return WinErrorMessage(code);
#else
    char message[1024];
    TS_ZERO(message);

    char* result = message;
    bool found = false;
    handle_strerror_r(found, result, strerror_r(code, message, sizeof(message)));

    if (found) {
        // Make sure message is nul-terminated.
        message[sizeof(message) - 1] = 0;
        // Remove trailing newlines (if any)
        for (size_t i = ::strlen(result); i > 0 && (result[i - 1] == '\n' || result[i - 1] == '\r'); result[--i] = 0) {}
        return UString::FromUTF8(result);
    }

    // At this point, the message is not found.
    return UString::Format(u"System error %d (0x%X)", {code, code});
#endif
}


//----------------------------------------------------------------------------
// Get the CPU time of the process in milliseconds.
//----------------------------------------------------------------------------

ts::MilliSecond ts::GetProcessCpuTime()
{
#if defined(TS_WINDOWS)

    ::FILETIME creation_time, exit_time, kernel_time, user_time;
    if (::GetProcessTimes(::GetCurrentProcess(), &creation_time, &exit_time, &kernel_time, &user_time) == 0) {
        throw ts::Exception(u"GetProcessTimes error", ::GetLastError());
    }
    return ts::Time::Win32FileTimeToMilliSecond(kernel_time) + ts::Time::Win32FileTimeToMilliSecond(user_time);

#else

    ::rusage usage;
    TS_ZERO(usage);
    if (::getrusage(RUSAGE_SELF, &usage) < 0) {
        throw ts::Exception(u"getrusage error", errno);
    }
    return MilliSecond(usage.ru_stime.tv_sec) * MilliSecPerSec +
           MilliSecond(usage.ru_stime.tv_usec) / MicroSecPerMilliSec +
           MilliSecond(usage.ru_utime.tv_sec) * MilliSecPerSec +
           MilliSecond(usage.ru_utime.tv_usec) / MicroSecPerMilliSec;

#endif
}


//----------------------------------------------------------------------------
// Get the virtual memory size of the process in bytes.
//----------------------------------------------------------------------------

size_t ts::GetProcessVirtualSize()
{
#if defined(TS_WINDOWS)

    ::PROCESS_MEMORY_COUNTERS_EX mem_counters;
    TS_ZERO(mem_counters);
    if (::GetProcessMemoryInfo(::GetCurrentProcess(), (::PROCESS_MEMORY_COUNTERS*)&mem_counters, sizeof(mem_counters)) == 0) {
        throw ts::Exception(u"GetProcessMemoryInfo error", ::GetLastError());
    }
    return size_t(mem_counters.PrivateUsage);

#elif defined(TS_LINUX)

    // On Linux, the VSIZE in pages is in the first field of /proc/self/statm.
    size_t vsize = 0;
    std::ifstream file("/proc/self/statm");
    file >> vsize;
    file.close();

    // Get page size in bytes.
    const long psize = ::sysconf(_SC_PAGESIZE);
    if (psize < 0) {
        throw ts::Exception(u"sysconf(_SC_PAGESIZE) error", errno);
    }
    return vsize * size_t(psize);

#elif defined(TS_MAC)

    // macOS implementation.

    // Get the virtual memory size using task_info (mach kernel).
    ::mach_task_basic_info_data_t taskinfo;
    TS_ZERO(taskinfo);
    ::mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    const ::kern_return_t status1 = ::task_info(::mach_task_self(), MACH_TASK_BASIC_INFO, ::task_info_t(&taskinfo), &count);
    if (status1 != KERN_SUCCESS) {
        throw ts::Exception(u"task_info error", errno);
    }
    return size_t(taskinfo.virtual_size);

#elif defined(TS_FREEBSD)

    // FreeBSD implementation.

    // Get the virtual memory size using procstat_getprocs() on current process.
    ::procstat* pstat = ::procstat_open_sysctl();
    if (pstat == nullptr) {
        throw ts::Exception(u"procstat_open_sysctl error", errno);
    }

    unsigned int kproc_count = 0;
    ::kinfo_proc* kproc = ::procstat_getprocs(pstat, KERN_PROC_PID, ::getpid(), &kproc_count);
    if (kproc == nullptr || kproc_count == 0) {
        throw ts::Exception(u"procstat_getprocs error", errno);
    }
    const size_t size = size_t(kproc->ki_size);

    ::procstat_freeprocs(pstat, kproc);
    ::procstat_close(pstat);
    return size;

#elif defined(TS_OPENBSD)

    // OpenBSD implementation.

    // Use the kvm library to get the process virtual size.
    ::kvm_t* kvm = ::kvm_open(nullptr, nullptr, nullptr, KVM_NO_FILES, "kvm_open");
    if (kvm == nullptr) {
        throw ts::Exception(u"kvm_open error", errno);
    }

    int count = 0;
    ::kinfo_proc* kinfo = ::kvm_getprocs(kvm, KERN_PROC_PID, ::getpid(), sizeof(::kinfo_proc), &count);
    if (kinfo == nullptr || count == 0) {
        throw ts::Exception(u"kvm_getprocs error", errno);
    }

    // The virtual memory size is text size + data size + stack size.
    // Cannot use p_vm_map_size, it is always zero.
    const long pagesize = ::sysconf(_SC_PAGESIZE);
    const size_t size = size_t((kinfo->p_vm_tsize + kinfo->p_vm_dsize + kinfo->p_vm_ssize) * pagesize);

    ::kvm_close(kvm);
    return size;

#elif defined(TS_DRAGONFLYBSD)

    // DragonFlyBSD implementation.

    // Similar to OpenBSD but some symbols have different names and kvm_getprocs() has no way
    // to describe the current size of struct kinfo_proc. Moreover, /dev/null must be passed as
    // execfile and corefile. Otherwise, a permission denied error is returned on /dev/mem.
    ::kvm_t* kvm = ::kvm_open("/dev/null", "/dev/null", nullptr, O_RDONLY, "kvm_open");
    if (kvm == nullptr) {
        throw ts::Exception(u"kvm_open error", errno);
    }

    int count = 0;
    ::kinfo_proc* kinfo = ::kvm_getprocs(kvm, KERN_PROC_PID, ::getpid(), &count);
    if (kinfo == nullptr || count == 0) {
        throw ts::Exception(u"kvm_getprocs error", errno);
    }

    // The virtual memory size is directly in kp_vm_map_size, in bytes.
    const size_t size = size_t(kinfo->kp_vm_map_size);

    ::kvm_close(kvm);
    return size;

#elif defined(TS_NETBSD)

    // NetBSD implementation.

    // Similar to OpenBSD but use struct kinfo_proc2 and kvm_getproc2().
    ::kvm_t* kvm = ::kvm_open(nullptr, nullptr, nullptr, KVM_NO_FILES, "kvm_open");
    if (kvm == nullptr) {
        throw ts::Exception(u"kvm_open error", errno);
    }

    int count = 0;
    ::kinfo_proc2* kinfo = ::kvm_getproc2(kvm, KERN_PROC_PID, ::getpid(), sizeof(::kinfo_proc2), &count);
    if (kinfo == nullptr || count == 0) {
        throw ts::Exception(u"kvm_getprocs error", errno);
    }

    // The virtual memory size is text size + data size + stack size.
    const long pagesize = ::sysconf(_SC_PAGESIZE);
    const size_t size = size_t((kinfo->p_vm_tsize + kinfo->p_vm_dsize + kinfo->p_vm_ssize) * pagesize);

    ::kvm_close(kvm);
    return size;

#else
    #error "ts::GetProcessVirtualSize not implemented on this system"
#endif
}


//----------------------------------------------------------------------------
// Ignore SIGPIPE. On UNIX systems: writing to a broken pipe returns an
// error instead of killing the process. On Windows systems: does nothing.
//----------------------------------------------------------------------------

void ts::IgnorePipeSignal()
{
#if !defined(TS_WINDOWS)
    ::signal(SIGPIPE, SIG_IGN);
#endif
}


//----------------------------------------------------------------------------
// Put standard input / output stream in binary mode.
//----------------------------------------------------------------------------

bool ts::SetBinaryModeStdin(Report& report)
{
#if defined(TS_WINDOWS)
    report.debug(u"setting standard input to binary mode");
    if (::_setmode(_fileno(stdin), _O_BINARY) < 0) {
        report.error(u"cannot set standard input to binary mode");
        Args* args = dynamic_cast<Args*>(&report);
        if (args != 0) {
            args->exitOnError();
        }
        return false;
    }
#endif
    return true;
}

bool ts::SetBinaryModeStdout(Report& report)
{
#if defined(TS_WINDOWS)
    report.debug(u"setting standard output to binary mode");
    if (::_setmode(_fileno(stdout), _O_BINARY) < 0) {
        report.error(u"cannot set standard output to binary mode");
        Args* args = dynamic_cast<Args*>(&report);
        if (args != 0) {
            args->exitOnError();
        }
        return false;
    }
#endif
    return true;
}


//----------------------------------------------------------------------------
// Check if the standard input/output/error is a terminal.
//----------------------------------------------------------------------------

#if defined(TS_WINDOWS)
namespace {
    // On Windows, only the DOS and PowerShell consoles are considered as terminal.
    // We also want to recognize as terminals the Cygwin and Msys consoles (mintty).
    bool StdHandleIsATerminal(::DWORD ns)
    {
        const ::HANDLE handle = ::GetStdHandle(ns);
        switch (::GetFileType(handle)) {
            case FILE_TYPE_CHAR: {
                // A native console (DOS or PowerShell).
                return true;
            }
            case FILE_TYPE_PIPE: {
                // Check if associated file name matches Cygwin or Msys pty name.
                // With mintty, the standard devices are named pipes. With Cygwin,
                // the name starts with \cygwin. With Msys, the name starts with \msys.
                // Then, if the device is the mintty console, the name contains -pty.
                // For actual pipes, -pty is replaced by -pipe.
                const ts::UString name = ts::WinDeviceName(handle).toLower();
                return (name.find(u"\\cygwin") != ts::NPOS || name.find(u"\\msys") != ts::NPOS) && name.find(u"-pty") != ts::NPOS;
            }
            default: {
                // Cannot be a terminal.
                return false;
            }
        }
    }
}
#endif

bool ts::StdInIsTerminal()
{
#if defined(TS_WINDOWS)
    return StdHandleIsATerminal(STD_INPUT_HANDLE);
#else
    return ::isatty(STDIN_FILENO);
#endif
}

bool ts::StdOutIsTerminal()
{
#if defined(TS_WINDOWS)
    return StdHandleIsATerminal(STD_OUTPUT_HANDLE);
#else
    return ::isatty(STDOUT_FILENO);
#endif
}

bool ts::StdErrIsTerminal()
{
#if defined(TS_WINDOWS)
    return StdHandleIsATerminal(STD_ERROR_HANDLE);
#else
    return ::isatty(STDERR_FILENO);
#endif
}


//----------------------------------------------------------------------------
// Get the name of a class from the @c type_info of an object.
//----------------------------------------------------------------------------

#if defined(TS_GCC)
#include <cxxabi.h>
#endif

ts::UString ts::ClassName(const std::type_info& info)
{
    UString name;
    const char* const rtti = info.name();
    if (rtti != nullptr) {
        // By default, use the plain RTTI name. Not always a pretty name.
        name.assignFromUTF8(rtti);
#if defined(TS_GCC)
        // With gcc and clang, this is a C++ mangled name.
        // Demangle it using the portable C++ ABI library.
        int status = 0;
        char* const demangled = abi::__cxa_demangle(rtti, nullptr, nullptr, &status);
        if (demangled != nullptr) {
            name.assignFromUTF8(demangled);
            ::free(demangled);
        }
#endif
        // Cleanup various initial decoration, depending on compiler.
        if (name.startWith(u"class ")) {
            name.erase(0, 6);
        }
        // MSC: `anonymous namespace'::
        // GCC: (anonymous namespace)::
        if (name.find(u"anonymous namespace") == 1 && name.find(u"::") == 21) {
            name.erase(0, 23);
        }
    }
    return name;
}
