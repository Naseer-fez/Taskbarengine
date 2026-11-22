#include "widget_host_model.h"

#include <algorithm>
#include <cmath>
#include <cJSON.h>

namespace te::widgets {

void Layout::Reset(const std::vector<Item>& items, float available_width, float gap) {
    items_ = items;
    available_width_ = std::max(available_width, 0.0f);
    gap_ = std::max(gap, 0.0f);
    dragging_ = false;
    Relayout();
}

bool Layout::Move(const std::string& id, size_t new_index) {
    const auto found = std::find_if(items_.begin(), items_.end(), [&id](const Item& item) { return item.id == id; });
    if (found == items_.end() || items_.empty()) return false;
    const size_t old_index = static_cast<size_t>(std::distance(items_.begin(), found));
    new_index = std::min(new_index, items_.size() - 1);
    if (old_index == new_index) return false;
    Item moved = *found;
    items_.erase(found);
    items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(new_index), moved);
    Relayout();
    return true;
}

bool Layout::MoveAt(float x) {
    if (!dragging_ || items_.empty()) return false;
    drag_x_ = x;
    size_t target = 0;
    while (target + 1 < items_.size() && x >= items_[target].x + items_[target].width * 0.5f) ++target;
    const bool changed = target != drag_index_;
    if (changed) {
        Item moved = items_[drag_index_];
        items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(drag_index_));
        items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(target), moved);
        drag_index_ = target;
        Relayout();
    }
    items_[drag_index_].x = drag_x_ - items_[drag_index_].width * 0.5f;
    return changed;
}

int Layout::HitTest(float x, float y, float height) const {
    if (y < 0.0f || y >= height) return -1;
    for (size_t i = 0; i < items_.size(); ++i) {
        const auto& item = items_[i];
        if (x >= item.x && x < item.x + item.width) return static_cast<int>(i);
    }
    return -1;
}

bool Layout::BeginDrag(size_t index, float pointer_x) {
    if (dragging_ || index >= items_.size()) return false;
    dragging_ = true;
    drag_index_ = index;
    drag_x_ = pointer_x;
    return true;
}

bool Layout::EndDrag() {
    const bool was_dragging = dragging_;
    dragging_ = false;
    Relayout();
    return was_dragging;
}

bool Layout::Tick(float delta_seconds) {
    constexpr float stiffness = 200.0f;
    constexpr float damping = 28.28f;
    const float dt = std::clamp(delta_seconds, 0.0f, 0.05f);
    bool moving = false;
    for (size_t i = 0; i < items_.size(); ++i) {
        auto& item = items_[i];
        const float target = item.x;
        const float force = -stiffness * (item.spring_position - target) - damping * item.spring_velocity;
        item.spring_velocity += force * dt;
        item.spring_position += item.spring_velocity * dt;
        if (std::fabs(item.spring_position - target) > 0.1f || std::fabs(item.spring_velocity) > 0.1f) moving = true;
        else { item.spring_position = target; item.spring_velocity = 0.0f; }
    }
    return moving;
}

void Layout::Relayout() {
    const size_t count = items_.size();
    if (count > 1) gap_ = std::min(gap_, available_width_ / static_cast<float>(count - 1));
    const float content_space = std::max(0.0f, available_width_ - gap_ * static_cast<float>(count > 0 ? count - 1 : 0));
    float requested_width = 0.0f;
    for (const auto& item : items_) {
        float width = item.measure.preferred_width;
        if (item.measure.min_width > 0.0f) width = std::max(width, item.measure.min_width);
        if (item.measure.max_width > 0.0f) width = std::min(width, item.measure.max_width);
        width = std::min(width, available_width_);
        requested_width += std::max(width, 0.0f);
    }
    const float fit_scale = requested_width > content_space && requested_width > 0.0f
        ? content_space / requested_width : 1.0f;
    float cursor = 0.0f;
    for (auto& item : items_) {
        float width = item.measure.preferred_width;
        if (item.measure.min_width > 0.0f) width = std::max(width, item.measure.min_width);
        if (item.measure.max_width > 0.0f) width = std::min(width, item.measure.max_width);
        item.width = std::max(0.0f, std::min(width, available_width_) * fit_scale);
        item.x = cursor;
        if (item.spring_position == 0.0f) item.spring_position = item.x;
        cursor += item.width + gap_;
    }
}

float PositionOrigin(float available_width, float content_width, float position_percent) {
    const float available = std::max(0.0f, available_width);
    const float travel = std::max(0.0f, available - std::clamp(content_width, 0.0f, available));
    const float percent = std::clamp(std::isfinite(position_percent) ? position_percent : 100.0f, 0.0f, 100.0f);
    return travel * (percent / 100.0f);
}

std::vector<std::string> ParseLayout(const cJSON* widgets_config) {
    std::vector<std::string> result;
    if (!widgets_config) return result;
    const cJSON* layout = cJSON_GetObjectItemCaseSensitive(widgets_config, "layout");
    if (cJSON_IsArray(layout)) {
        const cJSON* item = nullptr;
        cJSON_ArrayForEach(item, layout) {
            if (cJSON_IsString(item) && item->valuestring && item->valuestring[0]) result.emplace_back(item->valuestring);
        }
    } else if (cJSON_IsString(layout) && layout->valuestring) {
        std::string text = layout->valuestring;
        size_t begin = 0;
        while (begin < text.size()) {
            const size_t end = text.find(',', begin);
            std::string id = text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
            const size_t first = id.find_first_not_of(" \t\r\n");
            const size_t last = id.find_last_not_of(" \t\r\n");
            if (first != std::string::npos) result.emplace_back(id.substr(first, last - first + 1));
            if (end == std::string::npos) break;
            begin = end + 1;
        }
    }
    return result;
}

WidgetMeasurement SanitizeMeasurement(WidgetMeasurement measurement) {
    if (!std::isfinite(measurement.preferred_width) || !std::isfinite(measurement.min_width) ||
        !std::isfinite(measurement.max_width) || measurement.min_width < 0.0f ||
        measurement.max_width < measurement.min_width) {
        return { 80.0f, 40.0f, 240.0f };
    }
    measurement.min_width = std::clamp(measurement.min_width, 0.0f, 4096.0f);
    measurement.max_width = std::clamp(measurement.max_width, measurement.min_width, 4096.0f);
    measurement.preferred_width = std::clamp(measurement.preferred_width, measurement.min_width, measurement.max_width);
    return measurement;
}

bool PersistOrder(cJSON* widgets_config, const std::vector<Item>& items) {
    if (!widgets_config || !cJSON_IsObject(widgets_config)) return false;
    cJSON* layout = cJSON_CreateArray();
    if (!layout) return false;
    for (const auto& item : items) {
        cJSON* id = cJSON_CreateString(item.id.c_str());
        if (!id || !cJSON_AddItemToArray(layout, id)) {
            if (id) cJSON_Delete(id);
            cJSON_Delete(layout);
            return false;
        }
    }
    cJSON* old = cJSON_GetObjectItemCaseSensitive(widgets_config, "layout");
    if (old) {
        if (!cJSON_ReplaceItemInObjectCaseSensitive(widgets_config, "layout", layout)) {
            cJSON_Delete(layout);
            return false;
        }
    } else if (!cJSON_AddItemToObject(widgets_config, "layout", layout)) {
        cJSON_Delete(layout);
        return false;
    }
    cJSON* version = cJSON_GetObjectItemCaseSensitive(widgets_config, "schema_version");
    if (!version) cJSON_AddNumberToObject(widgets_config, "schema_version", 2);
    else if (cJSON_IsNumber(version)) { version->valueint = 2; version->valuedouble = 2; }
    return true;
}

int DelegateHitTest(const Layout& layout, float x, float y, float height,
                    HitTestDelegate delegate, void* context) {
    if (!delegate) return -1;
    const int index = layout.HitTest(x, y, height);
    if (index < 0) return -1;
    const auto& item = layout.Items()[static_cast<size_t>(index)];
    const WidgetHitTestResult result = delegate(item.id, x - item.x, y, context);
    return result.handled ? index : -1;
}

} // namespace te::widgets
