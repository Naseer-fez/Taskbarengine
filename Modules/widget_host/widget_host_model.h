#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <sdk/te_widget.h>
#include <cstdint>
#include <string>
#include <vector>

namespace te::widgets {

struct Item {
    std::string id;
    WidgetMeasurement measure{};
    float x = 0.0f;
    float width = 0.0f;
    float spring_position = 0.0f;
    float spring_velocity = 0.0f;
};

class Layout {
public:
    void Reset(const std::vector<Item>& items, float available_width, float gap);
    bool Move(const std::string& id, size_t new_index);
    bool MoveAt(float x);
    int HitTest(float x, float y, float height) const;
    const std::vector<Item>& Items() const { return items_; }
    std::vector<Item>& Items() { return items_; }
    bool IsDragging() const { return dragging_; }
    bool BeginDrag(size_t index, float pointer_x);
    bool EndDrag();
    bool Tick(float delta_seconds);

private:
    void Relayout();
    std::vector<Item> items_;
    float available_width_ = 0.0f;
    float gap_ = 8.0f;
    size_t drag_index_ = 0;
    float drag_x_ = 0.0f;
    bool dragging_ = false;
};

std::vector<std::string> ParseLayout(const struct cJSON* widgets_config);
WidgetMeasurement SanitizeMeasurement(WidgetMeasurement measurement);
bool PersistOrder(struct cJSON* widgets_config, const std::vector<Item>& items);
float PositionOrigin(float available_width, float content_width, float position_percent);
using HitTestDelegate = WidgetHitTestResult (*)(const std::string& id, float x, float y, void* context);
int DelegateHitTest(const Layout& layout, float x, float y, float height,
                    HitTestDelegate delegate, void* context);

} // namespace te::widgets
