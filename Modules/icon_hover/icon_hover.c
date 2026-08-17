#include <sdk/te_plugin.h>
#include <sdk/te_log.h>
#include <sdk/te_events.h>
#include <sdk/te_debug_trace.h>
#include "icon_hover_internal.h"
#include "uia_discovery.h"
#include "dcomp_overlay.h"
#include "icon_capture.h"
#include "frame_loop.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static TE_IconHoverState g_state = {0};

static void PluginLog(TE_LogLevel level, const char* fmt, ...)
{
    if (g_state.ctx.log) {
        va_list args;
        va_start(args, fmt);
        char buf[512];
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        g_state.ctx.log(level, "%s", buf);
    }
}
#define TE_LogWrite PluginLog

static const PluginMetadata g_metadata = {
    "icon_hover",
    "0.4.0",
    "TaskbarEngine",
    "Magnifies taskbar icons on hover using DirectComposition and smooth animation curves.",
    100,
    { 22621, 26100 }
};

static const char* g_curve_options[] = { "gaussian", "cubic", "linear", "cosine" };

static const SettingDescriptor g_descriptors[] = {
    { "scale", "Scale", "Maximum icon scale.", TE_SETTING_FLOAT, { .f = { 1.3f, 1.0f, 2.0f, 0.1f } } },
    { "radius", "Radius", "Magnification radius in px.", TE_SETTING_INT, { .i = { 120, 40, 300, 10 } } },
    { "curve", "Curve", "Magnification falloff curve.", TE_SETTING_ENUM, { .e = { "gaussian", g_curve_options, 4 } } },
    { "speed_ms", "Speed", "Settle animation speed in ms.", TE_SETTING_INT, { .i = { 150, 50, 500, 10 } } }
};

static const PluginSettings g_settings_schema = {
    g_descriptors,
    sizeof(g_descriptors) / sizeof(g_descriptors[0])
};

static void ParseConfig(const cJSON* config)
{
    if (!config) return;
    AcquireSRWLockExclusive(&g_state.icon_lock);
    const cJSON* scale = cJSON_GetObjectItemCaseSensitive(config, "scale");
    if (scale && cJSON_IsNumber(scale)) {
        g_state.max_scale = (float)scale->valuedouble;
        if (g_state.max_scale < 1.0f) g_state.max_scale = 1.0f;
        if (g_state.max_scale > 2.0f) g_state.max_scale = 2.0f;
    }
    const cJSON* radius = cJSON_GetObjectItemCaseSensitive(config, "radius");
    if (radius && cJSON_IsNumber(radius)) {
        g_state.radius = radius->valueint;
        if (g_state.radius < 40) g_state.radius = 40;
        if (g_state.radius > 300) g_state.radius = 300;
    }
    const cJSON* speed = cJSON_GetObjectItemCaseSensitive(config, "speed_ms");
    if (speed && cJSON_IsNumber(speed)) {
        g_state.speed_ms = speed->valueint;
        if (g_state.speed_ms < 50) g_state.speed_ms = 50;
        if (g_state.speed_ms > 500) g_state.speed_ms = 500;
    }
    const cJSON* curve = cJSON_GetObjectItemCaseSensitive(config, "curve");
    if (curve && cJSON_IsString(curve) && curve->valuestring) {
        if (strcmp(curve->valuestring, "gaussian") == 0) g_state.curve = TE_CURVE_GAUSSIAN;
        else if (strcmp(curve->valuestring, "cubic") == 0) g_state.curve = TE_CURVE_CUBIC;
        else if (strcmp(curve->valuestring, "linear") == 0) g_state.curve = TE_CURVE_LINEAR;
        else if (strcmp(curve->valuestring, "cosine") == 0) g_state.curve = TE_CURVE_COSINE;
    }
    ReleaseSRWLockExclusive(&g_state.icon_lock);
}

static HRESULT OnConfigChanged(uint32_t event_type, const void* event_data, void* user_data)
{
    (void)event_type; (void)user_data;
    if (event_data) {
        const TE_ConfigChangedEvent* evt = (const TE_ConfigChangedEvent*)event_data;
        AcquireSRWLockExclusive(&g_state.icon_lock);
        g_state.ctx.config = evt->new_config;
        ReleaseSRWLockExclusive(&g_state.icon_lock);
        ParseConfig(evt->new_config);
    }
    return S_OK;
}

static HRESULT OnShellHook(uint32_t event_type, const void* event_data, void* user_data)
{
    (void)event_type; (void)event_data; (void)user_data;
    /* Do not refresh UIA from shell hook notifications for now. Explorer sends
     * bursts of shell hook messages while its XAML taskbar tree is mutating;
     * repeated UIA descendant walks from inside that stream make Shell_TrayWnd
     * silently disappear. Initial discovery remains enabled at startup. */
    TE_DebugTrace("[TE-DBG] IconHover: ShellHook ignored for stability\n");
    return S_OK;
}

static HRESULT OnDpiChanged(uint32_t event_type, const void* event_data, void* user_data)
{
    (void)event_type; (void)user_data;
    if (event_data) {
        const TE_DpiChangedEvent* evt = (const TE_DpiChangedEvent*)event_data;
        g_state.ctx.dpi = evt->new_dpi;
    }
    return S_OK;
}

static BOOL CALLBACK EnumSecondaryTaskbars(HWND hwnd, LPARAM lParam)
{
    wchar_t class_name[64] = {0};
    GetClassNameW(hwnd, class_name, 64);
    if (wcscmp(class_name, L"Shell_SecondaryTrayWnd") == 0) {
        TE_IconHoverState* state = (TE_IconHoverState*)lParam;
        if (state->secondary_count < sizeof(state->secondary_taskbars) / sizeof(state->secondary_taskbars[0])) {
            state->secondary_taskbars[state->secondary_count++] = hwnd;
        }
    }
    return TRUE;
}

static HRESULT OnTaskbarMouse(uint32_t event_type, const void* event_data, void* user_data)
{
    (void)event_type; (void)event_data; (void)user_data;
    if (g_state.enabled) {
        /* Activate the frame loop timer on-demand when mouse is over taskbar.
         * TE_FrameLoopActivate is a no-op if the timer is already active. */
        TE_FrameLoopActivate();
    }
    return S_OK;
}

static HRESULT IconHover_Init(const PluginContext* ctx) {
    TE_DebugTraceFmt("[TE-DBG] IconHover: Init entering ctx=0x%p taskbar=0x%p\n",
                     (void*)ctx, ctx ? (void*)ctx->taskbar_hwnd : NULL);
    if (!ctx) return E_POINTER;
    ZeroMemory(&g_state, sizeof(g_state));
    InitializeSRWLock(&g_state.icon_lock);
    g_state.ctx = *ctx;
    g_state.max_scale = 1.3f;
    g_state.radius = 120;
    g_state.speed_ms = 150;
    g_state.curve = TE_CURVE_GAUSSIAN;
    ParseConfig(ctx->config);

    if (ctx->subscribe) {
        ctx->subscribe(TE_EVENT_CONFIG_CHANGED, OnConfigChanged, NULL);
        ctx->subscribe(TE_EVENT_SHELL_HOOK, OnShellHook, NULL);
        ctx->subscribe(TE_EVENT_DPI_CHANGED, OnDpiChanged, NULL);
        ctx->subscribe(TE_EVENT_TASKBAR_MOUSE, OnTaskbarMouse, NULL);
    }
    
    InterlockedExchange(&g_state.initialized, 1);
    TE_LogWrite(TE_LOG_INFO, "IconHover plugin initialized");
    TE_DebugTrace("[TE-DBG] IconHover: Init complete\n");
    return S_OK;
}

#define ICON_HOVER_INIT_TIMER_ID 0x54454948 /* 'TEIH' */
static UINT_PTR g_deferred_init_timer_id = 0;

static void CALLBACK DeferredInitTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
    (void)uMsg;
    (void)dwTime;
    /* Kill the timer first to prevent re-entrancy */
    TE_DebugTraceFmt("[TE-DBG] IconHover: Deferred timer fired hwnd=0x%p id=0x%p\n", (void*)hwnd, (void*)idEvent);
    KillTimer(hwnd, idEvent);
    g_deferred_init_timer_id = 0;
    
    TE_LogWrite(TE_LOG_INFO, "IconHover running deferred initialization...");
    
    HWND taskbar_hwnd = g_state.ctx.taskbar_hwnd;
    if (!taskbar_hwnd) {
        taskbar_hwnd = FindWindowW(L"Shell_TrayWnd", NULL);
    }
    if (!taskbar_hwnd) {
        taskbar_hwnd = FindWindowExW(NULL, NULL, L"Shell_TrayWnd", NULL);
    }
    if (!taskbar_hwnd) {
        TE_LogWrite(TE_LOG_WARN, "IconHover deferred init: Shell_TrayWnd not currently present");
        TE_DebugTrace("[TE-DBG] IconHover: Deferred init aborting, no taskbar HWND\n");
        return;
    }
    TE_DebugTraceFmt("[TE-DBG] IconHover: Deferred init using taskbar=0x%p\n", (void*)taskbar_hwnd);

    g_state.secondary_count = 0;
    EnumWindows(EnumSecondaryTaskbars, (LPARAM)&g_state);
    TE_DebugTraceFmt("[TE-DBG] IconHover: EnumSecondaryTaskbars count=%u\n", g_state.secondary_count);

    HRESULT capture_hr = TE_IconCaptureInit();
    TE_DebugTraceFmt("[TE-DBG] IconHover: IconCaptureInit hr=0x%08X\n", (unsigned int)capture_hr);
    if (FAILED(capture_hr)) return;

    HRESULT uia_hr = TE_UiaInit();
    TE_DebugTraceFmt("[TE-DBG] IconHover: UiaInit hr=0x%08X\n", (unsigned int)uia_hr);
    if (FAILED(uia_hr)) {
        TE_IconCaptureShutdown();
        return;
    }

    AcquireSRWLockExclusive(&g_state.icon_lock);
    TE_UiaDiscoverIcons(taskbar_hwnd, g_state.icons, TE_MAX_TASKBAR_ICONS, &g_state.icon_count);
    ReleaseSRWLockExclusive(&g_state.icon_lock);
    TE_DebugTraceFmt("[TE-DBG] IconHover: UiaDiscoverIcons count=%d\n", g_state.icon_count);

    HRESULT hr = TE_DcompInit(taskbar_hwnd);
    TE_DebugTraceFmt("[TE-DBG] IconHover: DcompInit hr=0x%08X\n", (unsigned int)hr);
    if (FAILED(hr)) {
        TE_UiaCleanup();
        TE_IconCaptureShutdown();
        return;
    }

    TE_DcompLoadIconSurfaces(&g_state);

    RECT tb_rect;
    GetWindowRect(taskbar_hwnd, &tb_rect);
    g_state.taskbar_rect = tb_rect;

    int tb_height = tb_rect.bottom - tb_rect.top;
    if (g_state.ctx.query_state) {
        StateValue height_val;
        if (SUCCEEDED(g_state.ctx.query_state("taskbar_resize.height", &height_val)) && height_val.type == TE_STATE_TYPE_INT) {
            tb_height = height_val.value.i;
        }
    }
    g_state.icon_size = (float)tb_height * 0.70f;
    if (g_state.icon_size < 24.0f) g_state.icon_size = 24.0f;

    hr = TE_FrameLoopStart(&g_state);
    TE_DebugTraceFmt("[TE-DBG] IconHover: FrameLoopStart hr=0x%08X\n", (unsigned int)hr);
    if (FAILED(hr)) {
        TE_LogWrite(TE_LOG_ERROR, "IconHover FrameLoopStart failed: 0x%08X", hr);
        TE_DcompShutdown();
        TE_UiaCleanup();
        TE_IconCaptureShutdown();
        return;
    }

    InterlockedExchange(&g_state.enabled, 1);
    TE_LogWrite(TE_LOG_INFO, "IconHover plugin fully enabled after deferred init");
    TE_DebugTrace("[TE-DBG] IconHover: Deferred init complete, plugin enabled\n");
}

static HRESULT IconHover_Enable(void) {
    TE_DebugTrace("[TE-DBG] IconHover: Enable entering\n");
    if (!g_state.initialized) {
        TE_LogWrite(TE_LOG_ERROR, "IconHover_Enable failed: plugin not initialized");
        return E_FAIL;
    }
    
    HWND taskbar = g_state.ctx.taskbar_hwnd;
    if (!taskbar || !IsWindow(taskbar)) {
        TE_LogWrite(TE_LOG_ERROR, "IconHover_Enable: no valid taskbar HWND");
        return E_HANDLE;
    }

    /* Use SetTimer instead of CreateTimerQueueTimer so the callback runs on
     * the thread that owns taskbar_hwnd (Explorer's UI thread).  The previous
     * WT_EXECUTEINTIMERTHREAD approach ran COM init, UIA discovery, DComp
     * window creation, and EnumWindows on a thread-pool thread, corrupting
     * Explorer's COM apartments and creating cross-thread window ownership. */
    TE_LogWrite(TE_LOG_INFO, "IconHover deferring enable to UI-thread timer");
    g_deferred_init_timer_id = SetTimer(taskbar, ICON_HOVER_INIT_TIMER_ID, 50, DeferredInitTimerProc);
    TE_DebugTraceFmt("[TE-DBG] IconHover: SetTimer returned id=0x%p err=%lu\n", (void*)g_deferred_init_timer_id, GetLastError());
    if (!g_deferred_init_timer_id) {
        TE_LogWrite(TE_LOG_ERROR, "IconHover SetTimer failed (err=%lu)", GetLastError());
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return S_OK;
}

static HRESULT IconHover_Disable(void) {
    TE_DebugTrace("[TE-DBG] IconHover: Disable entering\n");
    InterlockedExchange(&g_state.enabled, 0);
    
    if (g_deferred_init_timer_id && g_state.ctx.taskbar_hwnd) {
        KillTimer(g_state.ctx.taskbar_hwnd, ICON_HOVER_INIT_TIMER_ID);
        g_deferred_init_timer_id = 0;
    }

    TE_FrameLoopStop();
    TE_DcompShutdown();
    TE_IconCaptureShutdown();
    TE_UiaCleanup();
    
    AcquireSRWLockExclusive(&g_state.icon_lock);
    g_state.icon_count = 0;
    ReleaseSRWLockExclusive(&g_state.icon_lock);

    TE_LogWrite(TE_LOG_INFO, "IconHover plugin disabled");
    TE_DebugTrace("[TE-DBG] IconHover: Disable complete\n");
    return S_OK;
}

static HRESULT IconHover_Update(float deltaTime) {
    (void)deltaTime;
    return S_OK;
}

static HRESULT IconHover_Shutdown(void) {
    if (g_state.enabled) {
        IconHover_Disable();
    }
    if (g_state.ctx.unsubscribe) {
        g_state.ctx.unsubscribe(TE_EVENT_CONFIG_CHANGED, OnConfigChanged);
        g_state.ctx.unsubscribe(TE_EVENT_SHELL_HOOK, OnShellHook);
        g_state.ctx.unsubscribe(TE_EVENT_DPI_CHANGED, OnDpiChanged);
        g_state.ctx.unsubscribe(TE_EVENT_TASKBAR_MOUSE, OnTaskbarMouse);
    }
    InterlockedExchange(&g_state.initialized, 0);
    return S_OK;
}

static const PluginMetadata* IconHover_GetMetadata(void) {
    return &g_metadata;
}

static const PluginSettings* IconHover_GetSettings(void) {
    return &g_settings_schema;
}

static const PluginInterface g_plugin = {
    IconHover_Init,
    IconHover_Enable,
    IconHover_Disable,
    IconHover_Update,
    IconHover_Shutdown,
    IconHover_GetMetadata,
    IconHover_GetSettings
};

TE_EXPORT const PluginInterface* GetPluginInterface(void) {
    return &g_plugin;
}
