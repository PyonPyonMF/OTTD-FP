/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_runtime.cpp The first-person runtime: ties the bridge, the world mirror and the roles together. */

#include "../../stdafx.h"
#include "fp_runtime.h"
#include "../sim/fp_bridge.h"
#include "../sprites/fp_sprite_bridge.h"
#include "../../landscape.h"
#include "../../map_func.h"

#include "../../safeguards.h"

namespace fp {

/**
 * The runtime.
 * @return The only instance.
 */
/* static */ Runtime &Runtime::Get()
{
	static Runtime instance;
	return instance;
}

/**
 * Open the first-person view. The camera starts behind and above a point,
 * looking at it towards the top of the 2D view, so the 3D view shows what
 * the 2D view showed.
 * @param x X coordinate of the point to look at, in world units.
 * @param y Y coordinate of the point to look at, in world units.
 */
void Runtime::Activate(int x, int y)
{
	if (this->active) return;

	this->active = true;
	Bridge::Get().Acquire();
	this->ResetWorld();

	const double distance = 450.0;
	const double height = 220.0;
	const double yaw = -0.75 * std::numbers::pi; // Towards the north corner of the map, i.e. up in the 2D view.

	CameraPose pose;
	pose.yaw = yaw;
	pose.pitch = -std::atan2(height, distance);
	pose.fov_y = 60.0 * std::numbers::pi / 180.0;
	pose.position.x = this->scale.UnitsToMetres(x) - std::cos(yaw) * distance;
	pose.position.y = this->scale.UnitsToMetres(y) - std::sin(yaw) * distance;
	pose.position.z = this->GroundHeight(this->scale.UnitsToMetres(x), this->scale.UnitsToMetres(y)) + height;
	this->spectator.Place(pose);
}

/** Close the first-person view and let go of everything. */
void Runtime::Deactivate()
{
	if (!this->active) return;

	this->active = false;
	Bridge::Get().Release();
	this->mirror.Reset({}, this->scale);
	this->atlas.Reset();
	this->shown_sprites.clear();
	this->handoff.Deactivate();
}

/** Start building the world from scratch, for the current map and bridge session. */
void Runtime::ResetWorld()
{
	this->session = Bridge::Get().GetSession();
	this->scale = MetricScale{};
	this->mirror.Reset(ChunkGrid(Map::SizeX(), Map::SizeY()), this->scale);
	this->spectator.SetBounds(this->scale.TileLength() * Map::SizeX(), this->scale.TileLength() * Map::SizeY());
	this->atlas.Reset();
	this->reset_pending = true;
}

/**
 * Height of the ground, including foundations, at a position.
 * @param x X coordinate, in metres.
 * @param y Y coordinate, in metres.
 * @return The height, in metres.
 */
double Runtime::GroundHeight(double x, double y) const
{
	int ux = static_cast<int>(std::floor(this->scale.MetresToUnits(x)));
	int uy = static_cast<int>(std::floor(this->scale.MetresToUnits(y)));
	return this->scale.WorldZToMetres(GetSlopePixelZOutsideMap(ux, uy));
}

/**
 * Advance one frame.
 * @param input The input actions of this frame.
 * @param delta_ms Time since the previous frame, in milliseconds.
 * @param view_rect Screen rectangle of the 3D view.
 * @param visible Parts of \a view_rect not covered by other windows.
 */
void Runtime::Update(const InputState &input, uint delta_ms, const Rect &view_rect, std::vector<Rect> &&visible)
{
	if (!this->active) return;

	Bridge &bridge = Bridge::Get();
	if (bridge.GetSession() != this->session) {
		/* Another game: shown sprites stood in the old world. */
		this->shown_sprites.clear();
		this->ResetWorld();
	} else if (this->handoff.TakeContextLost()) {
		this->ResetWorld();
	}

	/* Long frames, e.g. while the game was busy loading, should not throw the camera across the map. */
	double dt = std::min(delta_ms / 1000.0, 0.1);
	this->spectator.Update(input, dt, [this](double x, double y) { return this->GroundHeight(x, y); });
	const CameraPose camera = this->spectator.GetCamera();

	bridge.SetInterest(
			static_cast<int>(this->scale.MetresToUnits(camera.position.x)),
			static_cast<int>(this->scale.MetresToUnits(camera.position.y)),
			static_cast<int>(this->scale.MetresToUnits(VIEW_DISTANCE)));

	this->dirty.clear();
	bridge.TakeDirtyChunks(this->dirty);
	this->mirror.MarkDirty(this->dirty);
	this->mirror.Update(camera.position, VIEW_DISTANCE, MESH_BUDGET, this->ops);

	ViewFrame frame;
	frame.active = true;
	frame.session = this->session;
	frame.camera = camera;
	frame.scale = this->scale;
	frame.view_distance = VIEW_DISTANCE;
	frame.view_rect = view_rect;
	frame.visible = std::move(visible);
	this->BuildBillboards(frame.billboards);

	AtlasChanges atlas_changes;
	this->atlas.TakeChanges(atlas_changes);
	this->handoff.PublishAtlas(std::move(atlas_changes));
	this->handoff.Publish(frame, std::move(this->ops), std::exchange(this->reset_pending, false));
}

/**
 * Collect the billboards of the shown sprites, putting their images into the atlas as needed.
 * @param[out] billboards Receives the billboards.
 */
void Runtime::BuildBillboards(std::vector<SpriteBillboard> &billboards)
{
	/* When the atlas is full, it starts over in the middle of the requests and voids the regions taken
	 * before; then the requests run once more, and only images that fit together are shown. */
	for (int attempt = 0; attempt < 2; attempt++) {
		uint32_t epoch = this->atlas.GetEpoch();
		billboards.clear();
		for (const ShownSprite &shown : this->shown_sprites) {
			std::optional<AtlasRegion> region = this->atlas.Request(shown.sprite, shown.palette);
			if (!region.has_value()) continue;

			SpriteBillboard &billboard = billboards.emplace_back();
			billboard.position = shown.position;
			billboard.region = *region;
			billboard.pixel_length = SpritePixelLength(this->scale, region->zoom);
			billboard.layer = shown.layer;
		}
		if (this->atlas.GetEpoch() == epoch) break;
	}
}

/**
 * Put the camera somewhere, e.g. for reproducible screenshots.
 * @param x X coordinate, in metres.
 * @param y Y coordinate, in metres.
 * @param height Height of the eye above the ground, in metres; at least Spectator::EYE_HEIGHT.
 * @param yaw Heading in radians, counter-clockwise from the X axis seen from above.
 * @param pitch Pitch in radians, positive is up.
 */
void Runtime::PlaceCamera(double x, double y, double height, double yaw, double pitch)
{
	CameraPose pose = this->spectator.GetCamera();
	pose.position = {x, y, this->GroundHeight(x, y) + std::max(height, Spectator::EYE_HEIGHT)};
	pose.yaw = yaw;
	pose.pitch = Clamp(pitch, -Spectator::MAX_PITCH, Spectator::MAX_PITCH);
	this->spectator.Place(pose);
}

/**
 * Show a sprite as billboard in front of the camera, at a distance that
 * suits its size. The sprite is raised so that its lowest pixel is at that
 * point, or on the ground where the point is under the ground.
 * @param layers The sprite, with its palette; several for a sprite of
 *               layers, like a vehicle, which are drawn in order.
 * @return False iff no layer could be decoded.
 */
bool Runtime::ShowSprite(std::span<const PalSpriteID> layers)
{
	if (!this->active || layers.empty()) return false;

	SpriteBridge &sprites = SpriteBridge::Get();
	double size = 0;
	double lift = 0;
	bool any = false;
	for (const PalSpriteID &layer : layers) {
		auto image = sprites.GetImage(layer.sprite, layer.pal);
		if (image == nullptr) continue;
		double pixel = SpritePixelLength(this->scale, image->zoom);
		size = std::max(size, std::max(image->width, image->height) * pixel);
		lift = std::max(lift, (image->y_offs + image->height) * pixel);
		any = true;
	}
	if (!any) return false;

	const CameraPose camera = this->spectator.GetCamera();
	const double distance = Clamp(size * 2.5, 10.0, 300.0);
	Vec3d anchor = camera.position + camera.Forward() * distance;
	anchor.z = std::max(anchor.z, this->GroundHeight(anchor.x, anchor.y)) + lift;

	for (size_t i = 0; i < layers.size(); i++) {
		this->shown_sprites.push_back({layers[i].sprite, layers[i].pal, anchor, static_cast<uint8_t>(std::min<size_t>(i, UINT8_MAX))});
	}
	if (this->shown_sprites.size() > MAX_SHOWN_SPRITES) {
		this->shown_sprites.erase(this->shown_sprites.begin(), this->shown_sprites.end() - MAX_SHOWN_SPRITES);
	}
	return true;
}

/** Remove all sprites shown with #ShowSprite(). */
void Runtime::ClearShownSprites()
{
	this->shown_sprites.clear();
}

} // namespace fp
