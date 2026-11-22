#pragma once

#include <sdk/te_events.h>
#include <sdk/te_plugin.h>

namespace te::widgets {

HRESULT Initialize(const PluginContext* context);
HRESULT Enable(void);
HRESULT Disable(void);
HRESULT Shutdown(void);
void OnConfigChanged(const TE_ConfigChangedData* data);
void OnTaskbarMouse(const TE_TaskbarMouseData* data);
void OnTaskbarClick(const TE_TaskbarClickData* data);

} // namespace te::widgets
