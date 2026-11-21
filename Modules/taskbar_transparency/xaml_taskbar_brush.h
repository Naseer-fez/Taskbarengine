#pragma once

#include "transparency_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Windows 11 22H2+ taskbars are painted by XAML and use the XAML brush path. */
bool TE_XamlTaskbarIsSupported(void);

/* Updates the desired brush and queues it on the taskbar XAML dispatcher. */
void TE_XamlTaskbarSetAppearance(const TE_TransparencySettings* settings);

/* Starts the XAML Diagnostics TAP watcher once for the current Explorer process. */
void TE_XamlTaskbarStart(void);

/* A tree watcher may be attached before the taskbar background element is discovered. */
bool TE_XamlTaskbarIsReady(void);

#ifdef __cplusplus
}
#endif
