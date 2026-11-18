# Built-in Widget Specifications

## Clock

The clock reads local time with Win32 time formatting and redraws once per second. `format` accepts Windows time patterns such as `HH:mm`, `hh:mm tt`, or `yyyy-MM-dd HH:mm:ss`. Optional `show_date` and `date_format` control a second date line. Font family, size, and ARGB text color are configurable. The preferred width is measured from formatted text. Left click has no action by default.

## System Monitor

The system monitor displays CPU and RAM percentages as compact text or bars. CPU samples use `GetSystemTimes`; memory uses `GlobalMemoryStatusEx`. The configured interval is clamped to 1–10 seconds; the default is 2 seconds. Display mode can select text, bars, or both. Text, bar, warning, and critical colors and both thresholds are configurable. GPU temperature is not implemented.

## Media

The media widget consumes the existing GSMTC session source and its property-change event bridge rather than polling Windows media sessions. It displays title and optional artist, with configurable text color, font size, and maximum width. Left click toggles play/pause; right click requests next track. Long titles are clipped within the configured width. Album art and marquee scrolling are not implemented.

## Shared Constraints

Widgets render compactly at taskbar height and inherit the transparent shared overlay. A per-widget tint override may be used for a card accent without painting an opaque panel. No built-in widget performs network I/O; weather remains deferred. Update work must stay bounded and use event callbacks or timers rather than busy loops.
