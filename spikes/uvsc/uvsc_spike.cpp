// Phase-0 UVSC feasibility spike. Throwaway: answers "can a native C++ program drive
// µVision through UVSC?", nothing more.
//
//   uvsc_spike <project.uvprojx> <run-to hex addr> <stops> <final hex addr>
//
// Launches a hidden µVision with the project and a UVSOCK port on its command line
// (UV4 -j0 -s<port> <project>), connects to it with UVSC, enters the simulator, resets, then calls DBG_RUN_TO_ADDRESS(<run-to>) <stops> times (each stop is
// a deterministic point), checks START/STATUS/STOP, runs to <final> (where the
// project's INI closes its LOG), and shuts down. State observation happens in the
// project's INI (sIfile) hooks, because memory reads need UVSOCK.h, which Keil's
// AN198 package does not ship. uvsc_min.h is generated at build time from the
// user's own UVSC_C.h by scripts/gen-uvsc-min.ps1; nothing of Keil's is committed.
#include <windows.h>
#include <tlhelp32.h>
#include <iphlpapi.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>
#include "uvsc_min.h"

using Clock = std::chrono::steady_clock;
static const auto t0 = Clock::now();
static double wall() { return std::chrono::duration<double>(Clock::now() - t0).count(); }

static int failures = 0;
static bool check(const char* op, UVSC_STATUS st, const char* detail = "") {
    std::printf("[%s] %-28s status=%d %s (t=%.1fs)\n", st == UVSC_STATUS_SUCCESS ? "PASS" : "FAIL",
                op, static_cast<int>(st), detail, wall());
    if (st != UVSC_STATUS_SUCCESS) ++failures;
    return st == UVSC_STATUS_SUCCESS;
}

static volatile LONG disconnected = 0;
static void on_uvsc(void*, UVSC_CB_TYPE type, void*) {
    if (type == UVSC_CB_DISCONNECTED) InterlockedExchange(&disconnected, 1);
}

static std::vector<DWORD> uv4_pids() {
    std::vector<DWORD> pids;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe{sizeof pe};
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
        if (_wcsicmp(pe.szExeFile, L"UV4.exe") == 0) pids.push_back(pe.th32ProcessID);
    CloseHandle(snap);
    return pids;
}

static bool port_listening(int port) {
    ULONG size = 0;
    GetTcpTable(nullptr, &size, FALSE);
    std::vector<char> buf(size);
    auto* table = reinterpret_cast<MIB_TCPTABLE*>(buf.data());
    if (GetTcpTable(table, &size, FALSE) != NO_ERROR) return false;
    for (DWORD i = 0; i < table->dwNumEntries; ++i)
        if (table->table[i].dwState == MIB_TCP_STATE_LISTEN &&
            ntohs(static_cast<u_short>(table->table[i].dwLocalPort)) == port) return true;
    return false;
}

template <class F> static F load(HMODULE dll, const char* name) {
    auto p = reinterpret_cast<F>(GetProcAddress(dll, name));
    if (!p) { std::printf("[FAIL] GetProcAddress(%s)\n", name); std::exit(2); }
    return p;
}

// Poll DBG_STATUS until the target stops. Polling is only for *completion*; where
// execution stops is decided by the run-to address, never by host time.
static bool wait_stopped(PFN_UVSC_DBG_STATUS status, int h, const char* what, double timeout_s) {
    const double start = wall();
    int running = 1, polls = 0;
    while (wall() - start < timeout_s) {
        UVSC_STATUS st = status(h, &running);
        ++polls;
        if (st != UVSC_STATUS_SUCCESS) return check("DBG_STATUS", st, what);
        if (!running) {
            std::printf("[PASS] %-28s stopped after %.1fs wall, %d polls\n", what, wall() - start, polls);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::printf("[FAIL] %-28s still running after %.0fs\n", what, timeout_s);
    ++failures;
    return false;
}

int main(int argc, char** argv) {
    if (argc != 5) {
        std::printf("usage: uvsc_spike <project.uvprojx> <run-to hex> <stops> <final hex>\n");
        return 2;
    }
    const std::string project = argv[1];
    const unsigned long long run_to = std::strtoull(argv[2], nullptr, 16);
    const int stops = std::atoi(argv[3]);
    const unsigned long long final_addr = std::strtoull(argv[4], nullptr, 16);
    const auto uv4_before = uv4_pids();

    HMODULE dll = LoadLibraryExA(R"(C:\Keil_v5\UV4\UVSC64.dll)", nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!dll) { std::printf("[FAIL] LoadLibrary UVSC64.dll err=%lu\n", GetLastError()); return 2; }
#define LOAD(fn) load<PFN_##fn>(dll, #fn)
    auto Version    = LOAD(UVSC_Version);
    auto Init       = LOAD(UVSC_Init);
    auto UnInit     = LOAD(UVSC_UnInit);
    auto Open       = LOAD(UVSC_OpenConnection);
    auto Close      = LOAD(UVSC_CloseConnection);
    auto SockVer    = LOAD(UVSC_GEN_UVSOCK_VERSION);
    auto DbgEnter   = LOAD(UVSC_DBG_ENTER);
    auto DbgExit    = LOAD(UVSC_DBG_EXIT);
    auto DbgReset   = LOAD(UVSC_DBG_RESET);
    auto DbgStart   = LOAD(UVSC_DBG_START_EXECUTION);
    auto DbgStop    = LOAD(UVSC_DBG_STOP_EXECUTION);
    auto DbgStatus  = LOAD(UVSC_DBG_STATUS);
    auto RunTo      = LOAD(UVSC_DBG_RUN_TO_ADDRESS);
#undef LOAD

    unsigned int lib = 0, sock = 0;
    Version(&lib, &sock);
    std::printf("[INFO] UVSC library %u, UVSOCK interface %u\n", lib, sock);

    // Measured: UVSC_Init succeeds only when uvMaxPort - uvMinPort == 9 (exactly
    // UVSC_MAX_CLIENTS ports); every other range returns UVSC_STATUS_FAILED.
    if (!check("UVSC_Init", Init(4823, 4832))) return 1;

    // Auto-start (uvCmd = UV4.exe, port AUTO) was tested and FAILS: UVSC spawns
    // "UV4.exe -j0 -s4823", gets no answer, returns UVSC_STATUS_FAILED after ~9 s and
    // leaves that UV4 running. It also cannot pass a project (PRJ_LOAD needs PRJDATA
    // from UVSOCK.h). So µVision is launched here with the project, and UVSC connects.
    const int uv_port = 4830;
    if (port_listening(uv_port)) { std::printf("[FAIL] port %d already in use\n", uv_port); return 1; }
    std::string cmd = std::string(R"("C:\Keil_v5\UV4\UV4.exe" -j0 -s)") + std::to_string(uv_port) +
                      " \"" + project + "\"";
    STARTUPINFOA si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        std::printf("[FAIL] launch UV4 err=%lu\n", GetLastError());
        return 1;
    }
    std::printf("[PASS] %-28s pid=%lu (t=%.1fs)\n", "Launch UV4 -j0 -s", pi.dwProcessId, wall());
    const double launch = wall();
    while (!port_listening(uv_port) && wall() - launch < 60)   // readiness, not a retry of a failed call
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    std::printf("[%s] %-28s after %.1fs\n", port_listening(uv_port) ? "PASS" : "FAIL",
                "UVSOCK port listening", wall() - launch);

    char name[] = "vwb-spike";
    int h = -1, port = uv_port;
    char logfile[MAX_PATH];
    ExpandEnvironmentStringsA("%TEMP%\\vwb-uvsc-socket.log", logfile, MAX_PATH);
    if (!check("OpenConnection (existing)", Open(name, &h, &port, nullptr, UVSC_RUNMODE_NORMAL,
                                                 on_uvsc, nullptr, logfile, 0, nullptr))) {
        UnInit();
        TerminateProcess(pi.hProcess, 1);
        return 1;
    }
    std::printf("[INFO] handle=%d port=%d\n", h, port);

    unsigned int major = 0, minor = 0;
    if (check("GEN_UVSOCK_VERSION", SockVer(h, &major, &minor)))
        std::printf("[INFO] UVSOCK version reported by uVision: %u.%u\n", major, minor);

    // DBG_ENTER loads the AXF, runs the INI, resets and starts its own run-to-main, and
    // returns before that run has even started: DBG_STATUS reads "stopped" at first.
    // Commands sent in that window are lost (attempts 1-2 ended in HardFault_Handler).
    // The exact handshake is the async DBG_STOP_EXECUTION (BPREASON) message, but that is
    // a UVSOCK_CMD from the missing UVSOCK.h, so the spike settles on host time here.
    // This sleep only lets µVision finish its own startup; it is not a timing model.
    auto settle = [&](const char* what) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        return wait_stopped(DbgStatus, h, what, 60);
    };
    bool ok = check("DBG_ENTER (simulator)", DbgEnter(h)) && settle("DBG_ENTER run-to-main settle");

    for (int i = 1; ok && i <= stops; ++i) {
        char what[64];
        std::snprintf(what, sizeof what, "RUN_TO 0x%llX #%d", run_to, i);
        ok = check(what, RunTo(h, run_to)) && wait_stopped(DbgStatus, h, what, 180);
    }

    if (ok) {   // free-running start/stop: stop point depends on host timing, by design
        ok = check("DBG_START_EXECUTION", DbgStart(h));
        int running = -1;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        check("DBG_STATUS while running", DbgStatus(h, &running));
        std::printf("[INFO] running=%d (expect 1)\n", running);
        ok = ok && check("DBG_STOP_EXECUTION", DbgStop(h)) && wait_stopped(DbgStatus, h, "stop", 30);
    }

    if (ok) {
        char what[64];
        std::snprintf(what, sizeof what, "RUN_TO final 0x%llX", final_addr);
        ok = check(what, RunTo(h, final_addr)) && wait_stopped(DbgStatus, h, what, 180);
    }

    if (ok) {   // reset, then the first stop again: must reproduce the first SNAP line
        ok = check("DBG_RESET", DbgReset(h)) && settle("DBG_RESET settle");
        ok = ok && check("RUN_TO after reset", RunTo(h, run_to)) &&
             wait_stopped(DbgStatus, h, "RUN_TO after reset", 180);
    }

    check("DBG_EXIT", DbgExit(h));
    check("CloseConnection", Close(h, 1));
    check("UVSC_UnInit", UnInit());
    std::printf("[INFO] disconnect callback seen=%ld\n", disconnected);
    if (WaitForSingleObject(pi.hProcess, 15000) == WAIT_OBJECT_0) {
        std::printf("[PASS] %-28s UV4 exited after CloseConnection(terminate)\n", "UV4 exit");
    } else {
        std::printf("[FAIL] %-28s UV4 still running 15 s after CloseConnection(terminate); killed\n", "UV4 exit");
        TerminateProcess(pi.hProcess, 1);
        ++failures;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    // Orphan check: any UV4 not present before we started, after a grace period.
    std::this_thread::sleep_for(std::chrono::seconds(3));
    int orphans = 0;
    for (DWORD pid : uv4_pids()) {
        bool preexisting = false;
        for (DWORD b : uv4_before) preexisting |= (b == pid);
        if (!preexisting) { ++orphans; std::printf("[INFO] orphan UV4 pid=%lu\n", pid); }
    }
    std::printf("[%s] CleanShutdown                orphan_uv4=%d\n", orphans ? "FAIL" : "PASS", orphans);
    if (orphans) ++failures;
    std::printf("[INFO] failures=%d\n", failures);
    return failures ? 1 : 0;
}
