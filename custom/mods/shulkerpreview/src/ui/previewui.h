#pragma once

#include "hashedstring.h"
#include "minecraftuirendercontext.h"
#include "nineslicehelper.h"
#include "resourcelocation.h"

namespace PreviewUi {
inline constexpr int Columns = 9;
inline constexpr int Rows = 3;
inline constexpr float SlotStride = 18.0f;
inline constexpr float SlotDrawSize = 17.5f;
inline constexpr float ItemDrawSize = 16.0f;
inline constexpr float ItemInset = (SlotStride - ItemDrawSize) * 0.5f;
inline constexpr float CountTextHeight = 6.0f;
inline constexpr float PanelPadding = 6.0f;
inline constexpr float PanelWidth = Columns * SlotStride + PanelPadding * 2.0f;
inline constexpr float PanelHeight = Rows * SlotStride + PanelPadding * 2.0f;
inline constexpr float PanelScreenMargin = 4.0f;
inline constexpr mce::Color White{1.0f, 1.0f, 1.0f, 1.0f};

struct Placement {
    float x;
    float y;
};

struct CachedTextures {
    mce::TexturePtr panel;
    mce::TexturePtr slot;
};

inline const HashedString& flushMaterial() {
    static const HashedString material("ui_flush");
    return material;
}

inline const NinesliceHelper& panelNineSlice() {
    static const NinesliceHelper slice(16.0f, 16.0f, 4.0f, 4.0f);
    return slice;
}

inline const TextMeasureData& defaultMeasure() {
    static const TextMeasureData measure = []() {
        TextMeasureData m{};
        m.fontSize = 1.0f;
        m.renderShadow = true;
        return m;
    }();
    return measure;
}

inline const TextMeasureData& smallMeasure() {
    static const TextMeasureData measure = []() {
        TextMeasureData m{};
        m.fontSize = 0.7f;
        m.renderShadow = true;
        return m;
    }();
    return measure;
}

inline const CaretMeasureData& defaultCaret() {
    static const CaretMeasureData caret{};
    return caret;
}

inline bool hasTexture(const mce::TexturePtr& texture) {
    return static_cast<bool>(texture.mClientTexture);
}

inline float clampRange(float value, float lo, float hi) {
    if (hi < lo)
        return lo;
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

inline Placement placePanel(
    MinecraftUIRenderContext& ctx,
    float tooltipX,
    float tooltipY,
    float tooltipHeight,
    bool anchorAbove)
{
    float x = tooltipX;
    float y = anchorAbove
        ? (tooltipY - PanelHeight)
        : (tooltipY + tooltipHeight);

    RectangleArea clip = ctx.getFullClippingRectangle();
    if (clip._x1 > clip._x0) {
        x = clampRange(
            x,
            clip._x0 + PanelScreenMargin,
            clip._x1 - PanelWidth - PanelScreenMargin);
    }
    if (clip._y1 > clip._y0) {
        y = clampRange(
            y,
            clip._y0 + PanelScreenMargin,
            clip._y1 - PanelHeight - PanelScreenMargin);
    }

    return {x, y};
}

inline RectangleArea makePanelRect(float x, float y) {
    return {
        x,
        x + PanelWidth,
        y,
        y + PanelHeight
    };
}

inline RectangleArea makeSlotRect(float x, float y) {
    return {
        x,
        x + SlotDrawSize,
        y,
        y + SlotDrawSize
    };
}

template <typename Fn>
inline void forEachSlot(float ox, float oy, Fn&& fn) {
    for (int idx = 0; idx < 27; ++idx) {
        int col = idx % Columns;
        int row = idx / Columns;
        fn(idx, ox + col * SlotStride, oy + row * SlotStride);
    }
}

inline CachedTextures& getTextures(MinecraftUIRenderContext& ctx) {
    static CachedTextures textures;
    textures.panel = ctx.getTexture(
        ResourceLocation("textures/ui/dialog_background_opaque", ResourceFileSystem::UserPackage),
        false);
    textures.slot = ctx.getTexture(
        ResourceLocation("textures/ui/item_cell", ResourceFileSystem::UserPackage),
        false);
    return textures;
}

inline void drawPanel(
    MinecraftUIRenderContext& ctx,
    const CachedTextures& textures,
    const RectangleArea& rect)
{
    if (!hasTexture(textures.panel))
        return;

    panelNineSlice().draw(ctx, rect, textures.panel.getClientTexture());
}

inline void drawSlot(
    MinecraftUIRenderContext& ctx,
    const CachedTextures& textures,
    const RectangleArea& rect)
{
    if (!hasTexture(textures.slot))
        return;

    glm::vec2 pos{rect._x0, rect._y0};
    glm::vec2 size{rect._x1 - rect._x0, rect._y1 - rect._y0};
    ctx.drawImage(
        textures.slot.getClientTexture(),
        pos,
        size,
        {0, 0},
        {1, 1},
        false);
}

inline constexpr mce::Color BundleNormal{0.40f, 0.40f, 1.00f, 1.0f};
inline constexpr mce::Color BundleFull{1.00f, 0.40f, 0.40f, 1.0f};
inline constexpr mce::Color BarBackground{0.0f, 0.0f, 0.0f, 1.0f};

inline mce::Color durabilityColor(float ratio) {
    if (ratio < 0.0f)
        ratio = 0.0f;
    if (ratio > 1.0f)
        ratio = 1.0f;
    return {1.0f - ratio, ratio, 0.0f, 1.0f};
}

inline void drawDurabilityBar(
    MinecraftUIRenderContext& ctx,
    float slotX,
    float slotY,
    float ratio)
{
    float bx = slotX + 2.0f;
    float by = slotY + 13.0f;

    RectangleArea bg{
        bx,
        bx + 13.0f,
        by,
        by + 2.0f
    };
    ctx.fillRectangle(bg, mce::Color{0, 0, 0, 1}, 1.0f);

    RectangleArea bar{
        bx,
        bx + 13.0f * ratio,
        by,
        by + 1.0f
    };
    ctx.fillRectangle(bar, durabilityColor(ratio), 1.0f);
}

inline void drawDurabilityValue(
    MinecraftUIRenderContext& ctx,
    float slotX,
    float slotY,
    const char* text,
    float ratio)
{
    RectangleArea rect{
        slotX + 1.0f,
        slotX + SlotDrawSize - 1.0f,
        slotY + 1.0f,
        slotY + 7.0f
    };

    ctx.drawDebugText(
        rect,
        text,
        durabilityColor(ratio),
        ui::TextAlignment::Left,
        1.0f,
        smallMeasure(),
        defaultCaret());
}

inline void drawBundleFullnessBar(
    MinecraftUIRenderContext& ctx,
    float slotX,
    float slotY,
    int weight)
{
    if (weight < 0)
        return;

    float ratio = clampRange(static_cast<float>(weight) / 64.0f, 0.0f, 1.0f);

    float bx = slotX + 2.0f;
    float by = slotY + 13.0f;

    RectangleArea bg{
        bx,
        bx + 13.0f,
        by,
        by + 2.0f
    };
    ctx.fillRectangle(bg, BarBackground, 1.0f);

    RectangleArea bar{
        bx,
        bx + 13.0f * ratio,
        by,
        by + 1.0f
    };
    ctx.fillRectangle(bar, weight >= 64 ? BundleFull : BundleNormal, 1.0f);
}

inline void drawStackCountText(
    MinecraftUIRenderContext& ctx,
    float slotX,
    float slotY,
    const char* text)
{
    float ax = slotX + SlotDrawSize - 0.5f;
    float ay = slotY + SlotDrawSize - 1.5f;

    RectangleArea rect{
        ax - 20.0f,
        ax,
        ay - CountTextHeight,
        ay
    };

    ctx.drawDebugText(
        rect,
        text,
        White,
        ui::TextAlignment::Right,
        1.0f,
        defaultMeasure(),
        defaultCaret());
}
} // namespace PreviewUi
