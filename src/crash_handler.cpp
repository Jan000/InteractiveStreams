// crash_handler.cpp – installs a SIGSEGV/SIGABRT handler BEFORE any static
// initializers run (GCC constructor priority 101 runs before user statics at ~65535).
// On non-GCC / Windows this compiles to nothing.
#if defined(__GNUC__) && !defined(_WIN32)

#include <csignal>
#include <cstdio>
#include <cstring>
#include <execinfo.h>   // backtrace, backtrace_symbols_fd
#include <unistd.h>     // STDERR_FILENO

static void crash_signal_handler(int sig) {
    const char* name = (sig == SIGSEGV) ? "SIGSEGV"
                     : (sig == SIGABRT) ? "SIGABRT"
                     : (sig == SIGFPE)  ? "SIGFPE"
                     : (sig == SIGILL)  ? "SIGILL"
                     : "UNKNOWN";
    // Write directly to stderr (async-signal-safe)
    char buf[128];
    int n = snprintf(buf, sizeof(buf),
        "\n[CRASH] Signal %d (%s) – stack trace:\n", sig, name);
    write(STDERR_FILENO, buf, n);

    void* frames[32];
    int count = backtrace(frames, 32);
    backtrace_symbols_fd(frames, count, STDERR_FILENO);

    // Re-raise to get a core dump and proper exit code
    signal(sig, SIG_DFL);
    raise(sig);
}

// priority 101: runs after glibc (0-100) but before any user-level static ctors
__attribute__((constructor(101)))
static void install_crash_handler() {
    struct sigaction sa{};
    sa.sa_handler = crash_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;  // restore default after first call
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGFPE,  &sa, nullptr);
    sigaction(SIGILL,  &sa, nullptr);
}

#endif // __GNUC__ && !_WIN32
