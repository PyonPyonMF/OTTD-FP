/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_window.cpp The first-person window. */

#include "../../stdafx.h"
#include "fp_window.h"
#include "fp_widget.h"
#include "../fp_hooks.h"
#include "../core/fp_rect.h"
#include "../runtime/fp_runtime.h"
#include "../../gfx_func.h"
#include "../../map_func.h"
#include "../../palette_func.h"
#include "../../settings_type.h"
#include "../../strings_func.h"
#include "../../timer/timer.h"
#include "../../timer/timer_window.h"
#include "../../viewport_func.h"
#include "../../window_func.h"
#include "../../window_gui.h"

#include "table/strings.h"

#include "../../safeguards.h"

namespace fp {

/** Radians the camera turns per pixel of mouse movement. */
static constexpr float LOOK_SENSITIVITY = 0.0035f;

/**
 * Point on the map at the centre of the main viewport.
 * @return The point, in world units.
 */
static Point MainViewportCentre()
{
	const Window *w = FindWindowById(WindowClass::MainWindow, 0);
	if (w != nullptr && w->viewport != nullptr) {
		const Viewport &vp = *w->viewport;
		Point pt = TranslateXYToTileCoord(vp, vp.left + vp.width / 2, vp.top + vp.height / 2);
		if (pt.x != -1) return pt;
	}
	return {static_cast<int>(Map::SizeX() * TILE_SIZE / 2), static_cast<int>(Map::SizeY() * TILE_SIZE / 2)};
}

/**
 * Whether the direction keys steer the first-person view rather than scroll the main viewport.
 * That is the case when its window has the focus, or when no window has it, e.g.
 * after it was opened from the console.
 * @return True iff the direction keys belong to the first-person view.
 */
static bool HasDirectionKeyFocus()
{
	if (_focused_window != nullptr) return _focused_window->window_class == WindowClass::FirstPersonView;
	return FindWindowById(WindowClass::FirstPersonView, 0) != nullptr;
}

/** Window with the first-person view; while it is open, the first-person runtime is active. */
struct FirstPersonWindow : Window {
	Point look{}; ///< Mouse movement for looking around since the last frame, in pixels.
	int wheel = 0; ///< Mouse wheel notches since the last frame; negative is up.
	bool renderer_running = false; ///< Whether the renderer was running when the view was last drawn.

	FirstPersonWindow(WindowDesc &desc, WindowNumber number) : Window(desc)
	{
		this->InitNested(number);
		Point centre = MainViewportCentre();
		Runtime::Get().Activate(centre.x, centre.y);
	}

	void Close(int data = 0) override
	{
		Runtime::Get().Deactivate();
		this->Window::Close(data);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget == WID_FPV_STATUS) size.height = std::max(size.height, GetCharacterHeight(FontSize::Normal) + WidgetDimensions::scaled.framerect.Vertical());
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		switch (widget) {
			case WID_FPV_VIEW: {
				/* While the renderer runs, the 3D view is copied over this. */
				GfxFillRect(r, PC_BLACK);
				RendererStatus status = Runtime::Get().GetHandoff().GetRendererStatus();
				if (!status.running) {
					std::string message = status.failed ? GetString(STR_FP_VIEW_RENDERER_FAILED, status.info) : GetString(STR_FP_VIEW_NO_RENDERER);
					DrawStringMultiLine(r.Shrink(WidgetDimensions::scaled.frametext), message, TextColour::FromString, {AlignmentH::Centre, AlignmentV::Middle});
				}
				break;
			}

			case WID_FPV_STATUS: {
				const Runtime &runtime = Runtime::Get();
				const Spectator &spectator = runtime.GetSpectator();
				const Vec3d &position = spectator.GetCamera().position;
				double tile_length = runtime.GetScale().TileLength();
				RendererStatus status = Runtime::Get().GetHandoff().GetRendererStatus();
				std::string text = GetString(STR_FP_VIEW_STATUS,
						static_cast<int64_t>(std::floor(position.x / tile_length)),
						static_cast<int64_t>(std::floor(position.y / tile_length)),
						static_cast<int64_t>(std::round(spectator.GetHeightAboveGround())),
						runtime.GetWorldMirror().GetStatistics().built_chunks,
						status.vehicles,
						static_cast<int64_t>(status.frame_ms * 10), 1);
				DrawString(r.Shrink(WidgetDimensions::scaled.framerect), text);
				break;
			}
		}
	}

	bool OnRightClick([[maybe_unused]] Point pt, WidgetID widget) override
	{
		if (widget != WID_FPV_VIEW || _scrolling_viewport) return false;

		/* Look around while the right mouse button is held; the window system then calls OnScroll().
		 * Right clicks do not focus windows, but looking around means the view is in use. */
		SetFocusedWindow(this);
		_scrolling_viewport = true;
		_cursor.fix_at = true;
		return true;
	}

	void OnScroll(Point delta) override
	{
		_cursor.fix_at = true;

		/* HandleViewportScroll() inverts the movement except for this mode; undo that to get the mouse movement. */
		int sign = _settings_client.gui.scroll_mode == ViewportScrollMode::ViewportRMBFixed ? 1 : -1;
		this->look.x += sign * delta.x;
		this->look.y += sign * delta.y;
	}

	void OnMouseWheel(int wheel, WidgetID widget) override
	{
		if (widget == WID_FPV_VIEW) this->wheel += wheel;
	}

	/**
	 * Parts of a screen rectangle that no window in front of this one covers.
	 * @param view The rectangle.
	 * @return The visible parts, as disjoint rectangles.
	 */
	std::vector<Rect> GetVisibleParts(const Rect &view) const
	{
		std::vector<Rect> area;
		if (this->IsShaded()) return area;

		Rect clipped = IntersectRects(view, {0, 0, _screen.width - 1, _screen.height - 1});
		if (IsEmptyArea(clipped)) return area;
		area.push_back(clipped);

		for (const Window *w : Window::IterateFromFront()) {
			if (w == this) break;
			SubtractRects(area, {w->left, w->top, w->left + w->width - 1, w->top + w->height - 1});
			if (area.empty()) break;
		}
		return area;
	}

	void OnRealtimeTick(uint delta_ms) override
	{
		InputState input;
		if (HasDirectionKeyFocus() && !EditBoxInGlobalFocus()) {
			if (_dirkeys.Test(DirectionKey::Up)) input.move_forward += 1.0f;
			if (_dirkeys.Test(DirectionKey::Down)) input.move_forward -= 1.0f;
			if (_dirkeys.Test(DirectionKey::Right)) input.move_right += 1.0f;
			if (_dirkeys.Test(DirectionKey::Left)) input.move_right -= 1.0f;
			input.boost = _shift_pressed;
		}
		input.move_up = static_cast<float>(-this->wheel);
		input.look_yaw = -this->look.x * LOOK_SENSITIVITY;
		input.look_pitch = -this->look.y * LOOK_SENSITIVITY;
		this->look = {};
		this->wheel = 0;

		Rect view = this->GetWidget<NWidgetBase>(WID_FPV_VIEW)->GetCurrentRect().Translate(this->left, this->top);
		Runtime::Get().Update(input, delta_ms, view, this->GetVisibleParts(view));
	}

	/** Refresh the status bar, and the view when the renderer started or stopped. */
	const IntervalTimer<TimerWindow> status_interval = {std::chrono::milliseconds(250), [this](auto) {
		this->SetWidgetDirty(WID_FPV_STATUS);

		bool running = Runtime::Get().GetHandoff().GetRendererStatus().running;
		if (running != this->renderer_running) {
			this->renderer_running = running;
			this->SetWidgetDirty(WID_FPV_VIEW);
		}
	}};
};

static constexpr std::initializer_list<NWidgetPart> _nested_fp_view_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Grey),
		NWidget(WWT_CAPTION, Colours::Grey, WID_FPV_CAPTION), SetStringTip(STR_FP_VIEW_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::Grey),
		NWidget(WWT_DEFSIZEBOX, Colours::Grey),
		NWidget(WWT_STICKYBOX, Colours::Grey),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Grey, WID_FPV_VIEW), SetMinimalSize(480, 270), SetFill(1, 1), SetResize(1, 1), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Grey, WID_FPV_STATUS), SetFill(1, 0), SetResize(1, 0), SetToolTip(STR_FP_VIEW_STATUS_TOOLTIP), EndContainer(),
		NWidget(WWT_RESIZEBOX, Colours::Grey),
	EndContainer(),
};

static WindowDesc _fp_view_desc(
	WindowPosition::Automatic, "fp_view", 800, 480,
	WindowClass::FirstPersonView, WindowClass::None,
	{},
	_nested_fp_view_widgets
);

/** Open the first-person window, or bring it to the front. */
void ShowFirstPersonWindow()
{
	AllocateWindowDescFront<FirstPersonWindow>(_fp_view_desc, 0);
}

/* Hook called by the window system; see fp_hooks.h. */

bool CapturesDirectionKeys()
{
	return HasDirectionKeyFocus();
}

} // namespace fp
