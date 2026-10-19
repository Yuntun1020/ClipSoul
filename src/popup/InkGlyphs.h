#pragma once

#include "ClipSoul/PopupLayout.h"
#include <d2d1.h>
#include <cwchar>
#include <map>
#include <string_view>

namespace ClipSoul::popup_detail {

enum class InkGlyph { Pin, PinFill, Close, Filter, Multi, Trash, Text, Link, Image, File,
                      Star, Note, Folder, Plus, Check, Up, Down, ArrowUp, ArrowDown,
                      Reset, External };

// Own one cache per window, on its drawing thread. Clear before releasing the
// window's factory; render-target recreation alone does not invalidate this cache.
class InkGlyphCache {
public:
    InkGlyphCache() = default;
    ~InkGlyphCache() { Clear(); }
    InkGlyphCache(const InkGlyphCache&) = delete;
    InkGlyphCache& operator=(const InkGlyphCache&) = delete;

    void Clear() noexcept {
        for (const auto& entry : geometries_) {
            if (entry.second) entry.second->Release();
        }
        geometries_.clear();
        if (stroke_) { stroke_->Release(); stroke_ = nullptr; }
        if (factory_) { factory_->Release(); factory_ = nullptr; }
    }

private:
    friend void DrawInkGlyph(ID2D1RenderTarget*, ID2D1Factory*,
                             ID2D1SolidColorBrush*, InkGlyph, const UiRect&, InkGlyphCache*);

    void Bind(ID2D1Factory* factory) {
        if (factory_ == factory) return;
        Clear();
        factory_ = factory;
        factory_->AddRef();
    }

    ID2D1StrokeStyle* Stroke() {
        if (!stroke_) {
            auto properties = D2D1::StrokeStyleProperties();
            properties.startCap = properties.endCap = properties.dashCap = D2D1_CAP_STYLE_ROUND;
            properties.lineJoin = D2D1_LINE_JOIN_ROUND;
            factory_->CreateStrokeStyle(properties, nullptr, 0, &stroke_);
        }
        return stroke_;
    }

    // Only DrawInkGlyph's static literals are used as keys; each path has a fixed
    // filled mode. Views borrow the literal storage without copying SVG text.
    ID2D1PathGeometry* Geometry(const wchar_t* data, bool filled) {
        auto entry = geometries_.try_emplace(std::wstring_view(data), nullptr).first;
        if (entry->second) return entry->second;
        ID2D1PathGeometry* geometry = nullptr;
        ID2D1GeometrySink* sink = nullptr;
        if (FAILED(factory_->CreatePathGeometry(&geometry))) return nullptr;
        if (SUCCEEDED(geometry->Open(&sink))) {
            const wchar_t* p = data;
            float x = 0, y = 0, start_x = 0, start_y = 0;
            wchar_t command = 0;
            bool figure = false;
            auto number = [&]() {
                while (*p == L' ' || *p == L',') ++p;
                wchar_t* end = nullptr;
                const float value = static_cast<float>(wcstod(p, &end));
                p = end;
                return value;
            };
            while (*p) {
                while (*p == L' ' || *p == L',') ++p;
                if (!*p) break;
                if ((*p >= L'A' && *p <= L'Z') || (*p >= L'a' && *p <= L'z')) command = *p++;
                const bool relative = command >= L'a' && command <= L'z';
                const wchar_t upper = relative ? command - 32 : command;
                if (upper == L'Z') {
                    if (figure) sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    figure = false; x = start_x; y = start_y; command = 0;
                    continue;
                }
                if (upper == L'M' || upper == L'L') {
                    float nx = number(), ny = number();
                    if (relative) { nx += x; ny += y; }
                    if (upper == L'M') {
                        if (figure) sink->EndFigure(D2D1_FIGURE_END_OPEN);
                        sink->BeginFigure(D2D1::Point2F(nx, ny), filled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
                        figure = true; start_x = nx; start_y = ny;
                        command = relative ? L'l' : L'L';
                    } else sink->AddLine(D2D1::Point2F(nx, ny));
                    x = nx; y = ny;
                } else if (upper == L'H' || upper == L'V') {
                    const float n = number();
                    if (upper == L'H') x = relative ? x + n : n;
                    else y = relative ? y + n : n;
                    sink->AddLine(D2D1::Point2F(x, y));
                } else if (upper == L'A') {
                    const float rx = number(), ry = number(), rotation = number();
                    const bool large = number() != 0, clockwise = number() != 0;
                    float nx = number(), ny = number();
                    if (relative) { nx += x; ny += y; }
                    sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(nx, ny), D2D1::SizeF(rx, ry), rotation,
                        clockwise ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                        large ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
                    x = nx; y = ny;
                } else if (upper == L'C') {
                    float x1 = number(), y1 = number(), x2 = number(), y2 = number(), nx = number(), ny = number();
                    if (relative) { x1 += x; y1 += y; x2 += x; y2 += y; nx += x; ny += y; }
                    sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(x1,y1), D2D1::Point2F(x2,y2), D2D1::Point2F(nx,ny)));
                    x = nx; y = ny;
                } else break;
            }
            if (figure) sink->EndFigure(D2D1_FIGURE_END_OPEN);
            const HRESULT result = sink->Close();
            sink->Release();
            if (SUCCEEDED(result)) {
                entry->second = geometry;
                return geometry;
            }
        }
        geometry->Release();
        return nullptr;
    }

    ID2D1Factory* factory_ = nullptr;
    ID2D1StrokeStyle* stroke_ = nullptr;
    std::map<std::wstring_view, ID2D1PathGeometry*> geometries_;
};

// The prototype's 16 x 16 SVG paths, rendered with its round 1.4px strokes.
// Pass a window-owned cache to reuse resources; nullptr keeps temporary ownership.
inline void DrawInkGlyph(ID2D1RenderTarget* target, ID2D1Factory* factory,
                         ID2D1SolidColorBrush* brush, InkGlyph icon, const UiRect& rect,
                         InkGlyphCache* cache = nullptr) {
    if (!target || !factory || !brush) return;
    InkGlyphCache temporary;
    InkGlyphCache& resources = cache ? *cache : temporary;
    resources.Bind(factory);
    ID2D1StrokeStyle* stroke = resources.Stroke();
    D2D1_MATRIX_3X2_F previous{};
    target->GetTransform(&previous);
    target->SetTransform(D2D1::Matrix3x2F::Scale(rect.Width() / 16.0f, rect.Height() / 16.0f) *
                         D2D1::Matrix3x2F::Translation(rect.left, rect.top) * previous);
    auto path = [&](const wchar_t* data, bool filled = false, float width = 1.4f) {
        if (auto* geometry = resources.Geometry(data, filled)) {
            if (filled) target->FillGeometry(geometry, brush);
            else target->DrawGeometry(geometry, brush, width, stroke);
        }
    };
    switch (icon) {
    case InkGlyph::Pin:
        path(L"M10 1.8 14.2 6l-2.4 1-2.6 2.6.3 3-1.2 1.2-6.1-6.1 1.2-1.2 3 .3L9 4.2z M5 11l-3.2 3.2"); break;
    case InkGlyph::PinFill:
        path(L"M10 1.4 14.6 6l-2.5 1.1-2.7 2.7.3 3.2-1.4 1.4-6.5-6.5 1.4-1.4 3.2.3L8.9 4z", true);
        path(L"M5.2 10.8l-3.6 3.6", false, 1.6f); break;
    case InkGlyph::Close: path(L"M4 4l8 8M12 4l-8 8"); break;
    case InkGlyph::Filter: path(L"M2.5 4h11M4.5 8h7M6.8 12h2.4"); break;
    case InkGlyph::Multi:
        target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(2.2f,2.2f,13.8f,13.8f),2.5f,2.5f),brush,1.4f,stroke);
        path(L"M5.2 8.2l2 2 3.8-4"); break;
    case InkGlyph::Trash: path(L"M2.8 4.2h10.4M6.3 4.2V2.8h3.4v1.4M4.2 4.2l.6 9h6.4l.6-9"); break;
    case InkGlyph::Text: path(L"M3 4h10M3 8h10M3 12h6"); break;
    case InkGlyph::Link:
        path(L"M6.8 9.2a2.6 2.6 0 0 0 3.7 0l2.2-2.2a2.6 2.6 0 0 0-3.7-3.7l-.9.9 M9.2 6.8a2.6 2.6 0 0 0-3.7 0L3.3 9a2.6 2.6 0 0 0 3.7 3.7l.9-.9"); break;
    case InkGlyph::Image:
        target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(2,2.8f,14,13.2f),2,2),brush,1.4f,stroke);
        target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(5.8f,6.3f),1.1f,1.1f),brush,1.4f,stroke);
        path(L"M2.5 11.5l3.3-3 2.4 2.1 2.1-1.8 3.2 2.9"); break;
    case InkGlyph::File: path(L"M4 1.8h5l3.2 3.2v9.2H4z M9 1.8V5h3.2"); break;
    case InkGlyph::Star: path(L"M8 1.8l1.9 3.9 4.3.6-3.1 3 .7 4.3L8 11.6l-3.8 2 .7-4.3-3.1-3 4.3-.6z"); break;
    case InkGlyph::Note: path(L"M11 2.5 13.5 5 6 12.5H3.5V10z"); break;
    case InkGlyph::Folder:
        path(L"M2 4.2c0-.7.5-1.2 1.2-1.2h3l1.5 1.6h5.1c.7 0 1.2.5 1.2 1.2v6c0 .7-.5 1.2-1.2 1.2H3.2c-.7 0-1.2-.5-1.2-1.2z"); break;
    case InkGlyph::Plus: path(L"M8 3v10M3 8h10",false,1.5f); break;
    case InkGlyph::Check: path(L"M3.5 8.5l3 3 6-6.5",false,2); break;
    case InkGlyph::Up: path(L"M8 13V3.5M4 7.5l4-4 4 4",false,1.5f); break;
    case InkGlyph::Down: path(L"M4 6l4 4 4-4",false,1.8f); break;
    case InkGlyph::ArrowUp: path(L"M4 10l4-4 4 4"); break;
    case InkGlyph::ArrowDown: path(L"M4 6l4 4 4-4"); break;
    // 设置页：恢复默认（↺）与外部链接（↗），与预览同一套 16 格路径。
    case InkGlyph::Reset: path(L"M3 8a5 5 0 1 0 1.6-3.7 M3 2.8v2.8h2.8"); break;
    case InkGlyph::External: path(L"M6 3h7v7M13 3 5 11", false, 1.6f); break;
    }
    target->SetTransform(previous);
}

} // namespace ClipSoul::popup_detail
