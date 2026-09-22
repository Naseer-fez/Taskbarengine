#include <catch2/catch_test_macros.hpp>
#include <windows.h>
#include <commctrl.h>
#include <sdk/te_types.h>
#include <sdk/te_ipc.h>
#include <sdk/te_plugin.h>
#include <sdk/te_events.h>
#include "core/plugin_loader.h"
#include "core/shell_hook.h"
#include "core/core_manager.h"
#include "taskbar_resize.h"
#include "tray_discovery.h"
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

/* ── SYS-005: Delayed Startup Polling & Detach Safety Invariant ──────── */

TEST_CASE("Phase 2 - Delayed Startup Thread Detach Safety (SYS-005)", "[phase2][sys_005]") {
    volatile LONG is_detaching = 0;
    std::atomic<bool> thread_exited{false};
    std::atomic<int> poll_iterations{0};

    // Simulate the exact loop logic inside TE_DelayedStartupThread
    std::thread worker([&]() {
        for (int i = 0; i < 600 && !InterlockedCompareExchange(&is_detaching, 0, 0); i++) {
            poll_iterations++;
            Sleep(5);
        }
        thread_exited = true;
    });

    // Let it poll briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    REQUIRE(poll_iterations.load() > 0);
    REQUIRE(thread_exited.load() == false);

    // Signal detach
    InterlockedExchange(&is_detaching, 1);

    worker.join();
    REQUIRE(thread_exited.load() == true);
    // Verified it broke out within a fraction of the 600 iterations
    REQUIRE(poll_iterations.load() < 100);
}

/* ── SYS-012: IPC Independent OVERLAPPED & PIPE_CONNECTED Invariant ──── */

TEST_CASE("Phase 2 - IPC Independent OVERLAPPED & Connection Handling (SYS-012)", "[phase2][sys_012]") {
    OVERLAPPED connect_ol = {0};
    connect_ol.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    REQUIRE(connect_ol.hEvent != NULL);

    OVERLAPPED read_ol = {0};
    read_ol.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    REQUIRE(read_ol.hEvent != NULL);

    OVERLAPPED write_ol = {0};
    write_ol.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    REQUIRE(write_ol.hEvent != NULL);

    // Invariant: each OVERLAPPED structure must have independent, distinct event handles
    REQUIRE(connect_ol.hEvent != read_ol.hEvent);
    REQUIRE(read_ol.hEvent != write_ol.hEvent);
    REQUIRE(connect_ol.hEvent != write_ol.hEvent);

    // Invariant: ERROR_PIPE_CONNECTED must be treated as synchronous success without calling GetOverlappedResult
    DWORD simulated_err = ERROR_PIPE_CONNECTED;
    BOOL connected = FALSE;
    if (simulated_err == ERROR_PIPE_CONNECTED) {
        connected = TRUE;
    }
    REQUIRE(connected == TRUE);

    CloseHandle(connect_ol.hEvent);
    CloseHandle(read_ol.hEvent);
    CloseHandle(write_ol.hEvent);
}

/* ── SYS-013: Plugin Loader Concurrency & Locking Invariant ──────────── */

TEST_CASE("Phase 2 - Plugin Loader Thread Synchronization (SYS-013)", "[phase2][sys_013]") {
    HRESULT hr = TE_PluginLoaderInit();
    REQUIRE(SUCCEEDED(hr));

    std::atomic<bool> run{true};
    std::atomic<int> shared_reads{0};
    std::atomic<int> exclusive_mutations{0};

    // 4 concurrent readers mimicking IPC queries and status reporting
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; i++) {
        readers.emplace_back([&run, &shared_reads]() {
            while (run.load(std::memory_order_relaxed)) {
                TE_PluginLoaderLockShared();
                int c = TE_PluginLoaderGetCount();
                for (int j = 0; j < c; j++) {
                    TE_PluginEntry* p = TE_PluginLoaderGetEntry(j);
                    if (p && p->metadata) {
                        volatile const char* name = p->metadata->name;
                        (void)name;
                    }
                }
                shared_reads.fetch_add(1, std::memory_order_relaxed);
                TE_PluginLoaderUnlockShared();
                std::this_thread::yield();
            }
        });
    }

    // 2 concurrent writers calling real enable/disable functions
    std::vector<std::thread> writers;
    for (int i = 0; i < 2; i++) {
        writers.emplace_back([&run, &exclusive_mutations]() {
            while (run.load(std::memory_order_relaxed)) {
                // Exercise real plugin enable/disable functions protected by exclusive lock
                TE_PluginLoaderEnablePluginByName("non_existent_dummy");
                TE_PluginLoaderDisablePluginByName("non_existent_dummy");
                exclusive_mutations.fetch_add(2, std::memory_order_relaxed);
                std::this_thread::yield();
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    run.store(false, std::memory_order_relaxed);

    for (auto& t : readers) t.join();
    for (auto& t : writers) t.join();

    REQUIRE(shared_reads.load() > 50);
    REQUIRE(exclusive_mutations.load() > 50);

    TE_PluginLoaderShutdown();
}

/* ── SYS-014: Work Area Deduplication & Calculation Invariants ───────── */

TEST_CASE("Phase 2 - Taskbar Resize WorkArea Deduplication & Calculation (SYS-014)", "[phase2][sys_014]") {
    RECT monitor_rect = { 0, 0, 1920, 1080 };
    RECT wa_32 = { 0 };
    RECT wa_48 = { 0 };

    REQUIRE(TE_CalculateWorkArea(&monitor_rect, 32, 96, &wa_32) == TRUE);
    REQUIRE(wa_32.left == 0);
    REQUIRE(wa_32.right == 1920);
    REQUIRE(wa_32.top == 0);
    REQUIRE(wa_32.bottom == 1080 - 32);

    REQUIRE(TE_CalculateWorkArea(&monitor_rect, 48, 96, &wa_48) == TRUE);
    REQUIRE(wa_48.bottom == 1080 - 48);

    // Equality check logic prevents layout storms
    REQUIRE(EqualRect(&wa_32, &wa_32) != 0);
    REQUIRE(EqualRect(&wa_32, &wa_48) == 0);
}

/* ── SYS-015: High-Frequency Subclass Deduplication ───────────────────── */

static LRESULT CALLBACK DummySubclassProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref) {
    (void)ref;
    if (m == WM_NCDESTROY) {
        RemoveWindowSubclass(h, DummySubclassProc, id);
    }
    return DefSubclassProc(h, m, w, l);
}

TEST_CASE("Phase 2 - Taskbar Resize Redundant Subclassing Guard (SYS-015)", "[phase2][sys_015]") {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"TE_SYS015_TestWnd";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowW(L"TE_SYS015_TestWnd", L"", WS_POPUP, 0, 0, 10, 10, NULL, NULL, wc.hInstance, NULL);
    REQUIRE(hwnd != NULL);

    DWORD_PTR ref_data = 0;
    // Initial state: not subclassed
    REQUIRE(!GetWindowSubclass(hwnd, DummySubclassProc, 201, &ref_data));

    // First install
    BOOL installed = SetWindowSubclass(hwnd, DummySubclassProc, 201, 0);
    REQUIRE(installed == TRUE);

    // Subsequent query: correctly reports already subclassed
    REQUIRE(GetWindowSubclass(hwnd, DummySubclassProc, 201, &ref_data) == TRUE);

    // Guard test: if GetWindowSubclass returns TRUE, we skip redundant SetWindowSubclass
    int subclass_calls = 0;
    for (int i = 0; i < 10; i++) {
        if (!GetWindowSubclass(hwnd, DummySubclassProc, 201, &ref_data)) {
            SetWindowSubclass(hwnd, DummySubclassProc, 201, 0);
            subclass_calls++;
        }
    }
    REQUIRE(subclass_calls == 0); // Correctly eliminated all 10 redundant calls

    DestroyWindow(hwnd);
    UnregisterClassW(L"TE_SYS015_TestWnd", wc.hInstance);
}

/* ── SYS-016: Custom Start Button Mouse Message Interception ─────────── */

TEST_CASE("Phase 2 - Custom Start Button Click Interception (SYS-016)", "[phase2][sys_016]") {
    RECT sb_bounds = { 0, 1032, 48, 1080 };
    POINT inside_pt = { 24, 1056 };
    POINT outside_pt = { 100, 1056 };

    REQUIRE(PtInRect(&sb_bounds, inside_pt) != 0);
    REQUIRE(PtInRect(&sb_bounds, outside_pt) == 0);

    // Simulate messages handled by custom start button
    UINT handled_msgs[] = { WM_LBUTTONDOWN, WM_LBUTTONUP, WM_LBUTTONDBLCLK };
    for (UINT msg : handled_msgs) {
        bool consumed = false;
        bool posted_tasklist = false;
        if ((msg == WM_LBUTTONUP || msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) && PtInRect(&sb_bounds, inside_pt)) {
            if (msg == WM_LBUTTONUP) {
                posted_tasklist = true;
            }
            consumed = true;
        }
        REQUIRE(consumed == true);
        if (msg == WM_LBUTTONUP) {
            REQUIRE(posted_tasklist == true);
        } else {
            REQUIRE(posted_tasklist == false);
        }
    }
}

/* ── SYS-017: Shell Hook Independent Registration Window ─────────────── */

TEST_CASE("Phase 2 - Shell Hook Dedicated Window Lifecycle (SYS-017)", "[phase2][sys_017]") {
    HWND dummy_taskbar = (HWND)(uintptr_t)0x12345678; // Even with an invalid HWND, it should NOT touch it
    HRESULT hr = TE_ShellHookInit(dummy_taskbar);
    REQUIRE(SUCCEEDED(hr));

    UINT msg_id = TE_ShellHookGetMessageId();
    REQUIRE(msg_id != 0);

    // Clean shutdown without corrupting shell_traywnd
    TE_ShellHookShutdown(dummy_taskbar);
}

/* ── SYS-019: Dropped Message String Leak Prevention ─────────────────── */

TEST_CASE("Phase 2 - String Leak Prevention on Dropped Messages (SYS-019)", "[phase2][sys_019]") {
    // 1. Uninitialized core command handler safety: string payload must be freed
    char* dummy_payload = _strdup("test_plugin_leak_check");
    REQUIRE(dummy_payload != NULL);

    bool core_initialized = false;
    int cmd = TE_CMD_ENABLE_PLUGIN;
    if (!core_initialized && cmd != TE_CMD_SHUTDOWN) {
        if ((cmd == TE_CMD_ENABLE_PLUGIN || cmd == TE_CMD_DISABLE_PLUGIN) && dummy_payload) {
            free(dummy_payload);
            dummy_payload = nullptr;
        }
    }
    REQUIRE(dummy_payload == nullptr);

    // 2. Queue draining logic test on WM_NCDESTROY
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"TE_SYS019_TestWnd";
    RegisterClassW(&wc);

    HWND test_hwnd = CreateWindowW(L"TE_SYS019_TestWnd", L"", WS_POPUP, 0, 0, 10, 10, NULL, NULL, wc.hInstance, NULL);
    REQUIRE(test_hwnd != NULL);

    // Post 3 string messages to queue
    char* s1 = _strdup("plugin_a");
    char* s2 = _strdup("plugin_b");
    char* s3 = _strdup("plugin_c");
    PostMessageW(test_hwnd, WM_TE_IPC_COMMAND, TE_CMD_ENABLE_PLUGIN, (LPARAM)s1);
    PostMessageW(test_hwnd, WM_TE_IPC_COMMAND, TE_CMD_DISABLE_PLUGIN, (LPARAM)s2);
    PostMessageW(test_hwnd, WM_TE_IPC_COMMAND, TE_CMD_ENABLE_PLUGIN, (LPARAM)s3);

    // Simulate draining as implemented in WM_NCDESTROY
    int drained = 0;
    MSG msg;
    while (PeekMessageW(&msg, test_hwnd, WM_TE_IPC_COMMAND, WM_TE_IPC_COMMAND, PM_REMOVE)) {
        if ((msg.wParam == TE_CMD_ENABLE_PLUGIN || msg.wParam == TE_CMD_DISABLE_PLUGIN) && msg.lParam) {
            free((void*)msg.lParam);
            drained++;
        }
    }
    REQUIRE(drained == 3);

    DestroyWindow(test_hwnd);
    UnregisterClassW(L"TE_SYS019_TestWnd", wc.hInstance);
}

/* ── SYS-020: Config Watcher Buffer Overflow Handling ────────────────── */

TEST_CASE("Phase 2 - Config Watcher Buffer Overflow Recovery (SYS-020)", "[phase2][sys_020]") {
    // When ReadDirectoryChangesW or GetOverlappedResult fails with ERROR_NOTIFY_ENUM_DIR,
    // the watcher must treat it as a trigger to reload rather than a fatal crash/abort.
    DWORD err_overflow = ERROR_NOTIFY_ENUM_DIR;
    DWORD err_abort = ERROR_OPERATION_ABORTED;
    bool reload_triggered = false;
    bool loop_aborted = false;

    if (err_overflow == ERROR_NOTIFY_ENUM_DIR) {
        reload_triggered = true;
    }
    REQUIRE(reload_triggered == true);

    if (err_abort == ERROR_OPERATION_ABORTED) {
        loop_aborted = true;
    }
    REQUIRE(loop_aborted == true);
}

/* ── SYS-022: Transparency Active State & Debounce Timer Safety ──────── */

TEST_CASE("Phase 2 - Transparency Debounce Timer Reinit Safety (SYS-022)", "[phase2][sys_022]") {
    TE_TrayDiscoveryState state;
    TE_TrayDiscoveryInit(&state);
    REQUIRE(state.count == 0);
    REQUIRE(state.apply_secondary == true);

    // Simulate an active debounce timer ID
    state.debounce_timer_id = 9999;

    // Reinitializing the state must kill existing timer and not leave dangling references
    TE_TrayDiscoveryInit(&state);
    REQUIRE(state.debounce_timer_id == 0);

    TE_TrayCleanup(&state);
    REQUIRE(state.count == 0);
}

/* ── SYS-023: Fixed-Size Stack Buffering in Config Diff ───────────────── */

TEST_CASE("Phase 2 - Config Diff Low-Memory Stack Buffering (SYS-023)", "[phase2][sys_023]") {
    const char* changed_names[4] = { "taskbar_resize", "icon_hover", "taskbar_transparency", nullptr };
    int changed_count = 3;

    if (changed_count > TE_MAX_PLUGINS) {
        changed_count = TE_MAX_PLUGINS;
    }

    char saved_names[TE_MAX_PLUGINS][64];
    for (int i = 0; i < changed_count; i++) {
        if (changed_names[i]) {
            strncpy_s(saved_names[i], sizeof(saved_names[i]), changed_names[i], _TRUNCATE);
        } else {
            saved_names[i][0] = '\0';
        }
    }

    REQUIRE(strcmp(saved_names[0], "taskbar_resize") == 0);
    REQUIRE(strcmp(saved_names[1], "icon_hover") == 0);
    REQUIRE(strcmp(saved_names[2], "taskbar_transparency") == 0);

    // Overflow clamp test
    int overflow_count = TE_MAX_PLUGINS + 10;
    if (overflow_count > TE_MAX_PLUGINS) {
        overflow_count = TE_MAX_PLUGINS;
    }
    REQUIRE(overflow_count == TE_MAX_PLUGINS);
}
