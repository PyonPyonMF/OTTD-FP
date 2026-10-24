/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_capture_check.cpp Tools to inspect and verify captured tile sprites: listings, images and a pixel-exact self-check. */

#include "../../stdafx.h"
#include "fp_capture_check.h"
#include "fp_tile_capture.h"
#include "../fp_hooks.h"
#include "../../blitter/factory.hpp"
#include "../../debug.h"
#include "../../core/backup_type.hpp"
#include "../../fileio_func.h"
#include "../../gfx_func.h"
#include "../../landscape.h"
#include "../../map_func.h"
#include "../../screenshot_type.h"
#include "../../settings_type.h"
#include "../../slope_func.h"
#include "../../spritecache.h"
#include "../../spriteloader/sprite_file_type.hpp"
#include "../../tile_map.h"
#include "../../viewport_func.h"
#include "../../window_func.h"
#include "../../window_gui.h"
#include "../../zoom_func.h"

#include "../../table/sprites.h"

#include "../../safeguards.h"

namespace fp {

/**
 * Create an image of an area of the 2D view.
 * @param left Left edge of the area, in viewport coordinates.
 * @param top Top edge of the area, in viewport coordinates.
 * @param right Right edge of the area, exclusive.
 * @param bottom Bottom edge of the area, exclusive.
 * @param zoom Zoom level to draw at.
 */
CaptureCanvas::CaptureCanvas(int left, int top, int right, int bottom, ZoomLevel zoom) : zoom(zoom)
{
	/* The 2D view only draws areas that start at whole pixels of the zoom level. */
	const int scale = ScaleByZoom(1, zoom);
	this->left = left & -scale;
	this->top = top & -scale;
	this->width = std::max(1, (right - this->left + scale - 1) / scale);
	this->height = std::max(1, (bottom - this->top + scale - 1) / scale);
	this->bytes_per_pixel = BlitterFactory::GetCurrentBlitter()->GetScreenDepth() / 8;
	this->pixels.resize(static_cast<size_t>(this->width) * this->height * this->bytes_per_pixel);
}

/**
 * Clear the image and draw on it, like a screenshot of the world does.
 * @param draw Draws the area it gets passed; it must not draw elsewhere.
 */
void CaptureCanvas::Draw(const std::function<void(const DrawPixelInfo &area)> &draw)
{
	std::fill(this->pixels.begin(), this->pixels.end(), 0);

	DrawPixelInfo area{
		.dst_ptr = this->pixels.data(),
		.left = this->left,
		.top = this->top,
		.width = ScaleByZoom(this->width, this->zoom),
		.height = ScaleByZoom(this->height, this->zoom),
		.pitch = this->width,
		.zoom = this->zoom,
	};

	/* The blitter must not draw on the screen, nor use the animation buffer of the screen. */
	AutoRestoreBackup screen_backup(_screen, {
		.dst_ptr = this->pixels.data(),
		.left = 0,
		.top = 0,
		.width = this->width,
		.height = this->height,
		.pitch = this->width,
		.zoom = ZoomLevel::Min
	});
	AutoRestoreBackup disable_anim_backup(_screen_disable_anim, true);
	draw(area);
}

/**
 * Count the pixels that differ from another image of the same size.
 * @param other The other image.
 * @param left Left edge of the region to compare, in pixels.
 * @param top Top edge of the region to compare, in pixels.
 * @param right Right edge of the region to compare, exclusive.
 * @param bottom Bottom edge of the region to compare, exclusive.
 * @return Number of differing pixels.
 */
size_t CaptureCanvas::CountDifferences(const CaptureCanvas &other, int left, int top, int right, int bottom) const
{
	assert(this->width == other.width && this->height == other.height && this->bytes_per_pixel == other.bytes_per_pixel);
	left = Clamp(left, 0, this->width);
	right = Clamp(right, left, this->width);
	top = Clamp(top, 0, this->height);
	bottom = Clamp(bottom, top, this->height);

	size_t differences = 0;
	const size_t bpp = this->bytes_per_pixel;
	for (int y = top; y < bottom; y++) {
		const size_t row = static_cast<size_t>(y) * this->width;
		for (int x = left; x < right; x++) {
			auto at = this->pixels.begin() + (row + x) * bpp;
			if (!std::equal(at, at + bpp, other.pixels.begin() + (row + x) * bpp)) differences++;
		}
	}
	return differences;
}

/**
 * Write the image as PNG.
 * @param path Where to write to.
 * @return True iff the image was written.
 */
bool CaptureCanvas::SavePng(const std::string &path) const
{
	const auto &providers = ProviderManager<ScreenshotProvider>::GetProviders();
	auto it = std::ranges::find(providers, "png", &ScreenshotProvider::GetName);
	if (it == std::end(providers)) return false;

	const size_t row_bytes = static_cast<size_t>(this->width) * this->bytes_per_pixel;
	auto copy_rows = [this, row_bytes](void *buf, uint y, uint pitch, uint n) {
		for (uint i = 0; i < n; i++) {
			std::copy_n(this->pixels.begin() + (y + i) * row_bytes, row_bytes, static_cast<uint8_t *>(buf) + static_cast<size_t>(i) * pitch * this->bytes_per_pixel);
		}
	};
	return (*it)->MakeImage(path, copy_rows, this->width, this->height, this->bytes_per_pixel * 8, _cur_palette.palette);
}

/**
 * Name of a tile type.
 * @param type The tile type.
 * @return The name.
 */
static std::string_view TileTypeName(TileType type)
{
	switch (type) {
		case TileType::Clear: return "clear";
		case TileType::Railway: return "railway";
		case TileType::Road: return "road";
		case TileType::House: return "house";
		case TileType::Trees: return "trees";
		case TileType::Station: return "station";
		case TileType::Water: return "water";
		case TileType::Void: return "void";
		case TileType::Industry: return "industry";
		case TileType::TunnelBridge: return "tunnel or bridge";
		case TileType::Object: return "object";
		default: return "?";
	}
}

/**
 * Describe a slope.
 * @param slope The slope.
 * @return Its raised corners and flags, like "N+E steep", plus the raw value.
 */
static std::string DescribeSlope(Slope slope)
{
	if (slope == SLOPE_FLAT) return "flat";

	std::string text;
	for (auto [corner, name] : {std::pair{SLOPE_N, "N"}, {SLOPE_E, "E"}, {SLOPE_S, "S"}, {SLOPE_W, "W"}}) {
		if ((slope & corner) == 0) continue;
		if (!text.empty()) text += '+';
		text += name;
	}
	if (IsSteepSlope(slope)) text += " steep";
	if (IsHalftileSlope(slope)) text += " half-tile";
	return fmt::format("{} (0x{:02x})", text, to_underlying(slope));
}

/**
 * Describe a captured sprite, including where its graphics come from:
 * the base set or the NewGRF that provides it.
 * @param sprite The sprite.
 * @return The description.
 */
static std::string DescribeSprite(const CapturedSprite &sprite)
{
	const SpriteID id = GB(sprite.image, 0, SPRITE_WIDTH);
	std::string text = fmt::format("sprite {}", id);

	if (id == SPR_EMPTY_BOUNDING_BOX) {
		text += " (empty bounding box)";
	} else if (SpriteExists(id)) {
		const SpriteFile *file = GetOriginFile(id);
		if (file != nullptr) format_append(text, " ({} #{})", file->GetSimplifiedFilename(), GetSpriteLocalID(id));
	}
	if (HasBit(sprite.image, PALETTE_MODIFIER_COLOUR)) text += " recoloured";
	if (HasBit(sprite.image, PALETTE_MODIFIER_TRANSPARENT)) text += " transparent";

	if (sprite.pal != PAL_NONE) format_append(text, ", palette {}", sprite.pal);
	format_append(text, ", at ({}, {})", sprite.offset.x, sprite.offset.y);
	if (sprite.sub.has_value()) format_append(text, ", part ({}, {})-({}, {})", sprite.sub->left, sprite.sub->top, sprite.sub->right, sprite.sub->bottom);
	return text;
}

/**
 * Describe the sprites of a captured tile, one line each.
 * Positions are relative to the north corner of the tile at its base height,
 * in pixels at ZoomLevel::Min; boxes are in world units relative to that corner.
 * @param capture The capture holding the tile.
 * @param tile The tile.
 * @return The lines.
 * @pre The game state lock is held, as the origin of sprites is looked up.
 */
std::vector<std::string> DescribeTileSprites(const TileSpriteCapture &capture, const TileSprites &tile)
{
	std::vector<std::string> lines;
	lines.push_back(fmt::format("Tile {} x {} (0x{:x}), {}: slope {} at z {}; ground at z {}, slope {}.",
			TileX(tile.tile), TileY(tile.tile), tile.tile.base(), TileTypeName(GetTileType(tile.tile)),
			DescribeSlope(tile.slope), tile.origin.z, tile.ground_z, DescribeSlope(tile.ground_slope)));

	size_t children = 0;
	for (const CapturedParentSprite &parent : capture.Parents(tile)) children += parent.child_count;
	lines.push_back(fmt::format("  {} ground, {} parent and {} child sprites.", tile.ground_count, tile.parent_count, children));

	uint index = 0;
	for (const CapturedSprite &sprite : capture.Ground(tile)) {
		lines.push_back(fmt::format("  ground {}: {}", index++, DescribeSprite(sprite)));
	}

	index = 0;
	for (const CapturedParentSprite &parent : capture.Parents(tile)) {
		std::string foundation;
		for (uint part = 0; part < tile.foundations.size(); part++) {
			if (tile.foundations[part] == tile.first_parent + index) foundation = part == to_underlying(CapturedFoundation::Normal) ? ", foundation" : ", half-tile foundation";
		}
		lines.push_back(fmt::format("  parent {}: {}, box ({}, {}, {})-({}, {}, {}){}", index++, DescribeSprite(parent.sprite),
				parent.bb_min.x, parent.bb_min.y, parent.bb_min.z, parent.bb_max.x, parent.bb_max.y, parent.bb_max.z, foundation));
		for (const CapturedSprite &child : capture.Children(parent)) {
			lines.push_back(fmt::format("    child: {}", DescribeSprite(child)));
		}
	}
	return lines;
}

/**
 * Keep a name safe to use as file name in the screenshot directory.
 * @param name The name.
 * @return The name with only letters, digits, '-' and '_'.
 */
static std::string SafeFileName(std::string_view name)
{
	std::string safe;
	for (char c : name) safe += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
	return safe;
}

/**
 * Where an image is written.
 * @param name Name of the image, without extension.
 * @return Path of the image in the screenshot directory.
 */
static std::string ImagePath(std::string_view name)
{
	return fmt::format("{}{}.png", FiosGetScreenshotDir(), SafeFileName(name));
}

/**
 * Collect the tiles the 2D view may draw something of in an area, in the
 * order it visits them (see ViewportAddLandscape()): by row, then column.
 * The set is generous; tiles whose sprites miss the area cost time only.
 * @param left Left edge of the area, in viewport coordinates.
 * @param top Top edge of the area, in viewport coordinates.
 * @param right Right edge of the area.
 * @param bottom Bottom edge of the area.
 * @param[out] tiles Receives the tiles.
 * @return False iff positions beyond the map were skipped.
 */
static bool CollectTilesForArea(int left, int top, int right, int bottom, std::vector<TileIndex> &tiles)
{
	/* Higher tiles, bridges and buildings further south reach up into the area. */
	uint highest = 0;
	for (const auto tile : Map::Iterate()) highest = std::max(highest, TileHeight(tile));
	const int reach = (highest + _settings_game.construction.max_bridge_height) * TILE_HEIGHT + MAX_BUILDING_PIXELS;

	Point upper_left = InverseRemapCoords(left, top);
	Point upper_right = InverseRemapCoords(right, top);
	Point lower_left = InverseRemapCoords(left, bottom);
	const int tile_size = static_cast<int>(TILE_SIZE);
	int left_column = (upper_left.y - upper_left.x) / tile_size - 2;
	int right_column = (upper_right.y - upper_right.x) / tile_size + 2;
	int first_row = (upper_left.x + upper_left.y) / tile_size - 4;
	int last_row = (lower_left.x + lower_left.y) / tile_size + 4 + reach / tile_size;

	bool inside = true;
	tiles.clear();
	for (int row = first_row; row <= last_row; row++) {
		for (int column = left_column; column <= right_column; column++) {
			if (((row + column) & 1) != 0) continue;
			int x = (row - column) / 2;
			int y = (row + column) / 2;
			if (x < 0 || y < 0 || x >= static_cast<int>(Map::SizeX()) || y >= static_cast<int>(Map::SizeY())) {
				inside = false;
				continue;
			}
			tiles.push_back(TileXY(x, y));
		}
	}
	return inside;
}

/**
 * Draw the main viewport the way the screen shows it, vehicles and signs included.
 * @return The image, if there is a main viewport.
 */
static std::optional<CaptureCanvas> DrawMainViewport()
{
	const Window *w = FindWindowById(WindowClass::MainWindow, 0);
	if (w == nullptr || w->viewport == nullptr) return std::nullopt;

	const Viewport &vp = *w->viewport;
	std::optional<CaptureCanvas> canvas{std::in_place, vp.virtual_left, vp.virtual_top, vp.virtual_left + vp.virtual_width, vp.virtual_top + vp.virtual_height, vp.zoom};
	canvas->Draw([&vp](const DrawPixelInfo &area) {
		/* ViewportDoDraw() draws relative to _cur_dpi, which is in screen coordinates. */
		DrawPixelInfo screen{.dst_ptr = area.dst_ptr, .left = vp.left, .top = vp.top, .width = vp.width, .height = vp.height, .pitch = area.pitch, .zoom = ZoomLevel::Min};
		AutoRestoreBackup dpi_backup(_cur_dpi, &screen);
		ViewportDoDraw(vp, vp.virtual_left, vp.virtual_top, vp.virtual_left + vp.virtual_width, vp.virtual_top + vp.virtual_height);
	});
	return canvas;
}

/**
 * Prove, pixel by pixel, that a capture holds everything the 2D view draws and
 * that capturing leaves the 2D view alone.
 *
 * First the landscape around a tile is drawn the way the 2D view draws it, as
 * reference. Then all tiles of the area are captured, and the captured sprites
 * are drawn again with the same blitter, sorter and settings; the result must
 * equal the reference. Sprites that straddle the edge of the area are clipped
 * by both, but may be combined differently; therefore only the inner part is
 * compared. Finally the main viewport, drawn before and after capturing, must
 * not have changed a single pixel.
 *
 * Tile selections and highlights are drawn by the reference only, so the check
 * expects none, as on the title screen.
 * @param centre The tile in the middle of the area.
 * @param radius Number of tiles around \a centre whose landscape is compared.
 * @param name If not empty, the images are written to the screenshot directory with this name as prefix.
 * @return The outcome.
 * @pre The game state lock is held.
 */
CaptureCheckResult CheckTileCapture(TileIndex centre, uint radius, std::string_view name)
{
	static constexpr ZoomLevel ZOOM = ZoomLevel::Normal;
	static constexpr int MARGIN = 2048; ///< Margin around the compared part, in viewport coordinates: two tiles.

	CaptureCheckResult result;

	/* The compared part: a rectangle inside the diamond of tiles around the centre tile. */
	const Point middle = RemapCoords(TileX(centre) * TILE_SIZE + TILE_SIZE / 2, TileY(centre) * TILE_SIZE + TILE_SIZE / 2, TilePixelHeight(centre));
	const int half_width = (2 * radius + 1) * TILE_PIXELS * ZOOM_BASE / 2;
	const int half_height = (2 * radius + 1) * TILE_PIXELS * ZOOM_BASE / 4;
	CaptureCanvas reference(middle.x - half_width - MARGIN, middle.y - half_height - MARGIN, middle.x + half_width + MARGIN, middle.y + half_height + MARGIN, ZOOM);
	CaptureCanvas replay = reference;

	std::vector<TileIndex> tiles;
	result.beyond_map = !CollectTilesForArea(reference.Left(), reference.Top(), reference.Left() + ScaleByZoom(reference.Width(), ZOOM), reference.Top() + ScaleByZoom(reference.Height(), ZOOM), tiles);

	std::optional<CaptureCanvas> view_before = DrawMainViewport();
	reference.Draw([](const DrawPixelInfo &area) { DrawLandscapeLikeViewport(area); });

	TileSpriteCapture capture;
	auto start = std::chrono::steady_clock::now();
	for (TileIndex tile : tiles) CaptureTileSprites(tile, capture);
	result.capture_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	result.tiles = static_cast<uint>(capture.tiles.size());
	result.sprites = capture.SpriteCount();

	replay.Draw([&capture](const DrawPixelInfo &area) { DrawCapturedTileSprites(capture, area); });
	std::optional<CaptureCanvas> view_after = DrawMainViewport();

	const int margin = UnScaleByZoom(MARGIN, ZOOM);
	result.width = reference.Width() - 2 * margin;
	result.height = reference.Height() - 2 * margin;
	result.replay_differences = reference.CountDifferences(replay, margin, margin, reference.Width() - margin, reference.Height() - margin);

	if (view_before.has_value() && view_after.has_value()) {
		result.view_width = view_before->Width();
		result.view_height = view_before->Height();
		result.view_differences = view_before->CountDifferences(*view_after, 0, 0, view_before->Width(), view_before->Height());
	}

	if (!name.empty()) {
		auto save = [&result](const CaptureCanvas &canvas, std::string_view file) {
			std::string path = ImagePath(file);
			if (canvas.SavePng(path)) result.files.push_back(std::move(path));
		};
		save(reference, fmt::format("{}_2d", name));
		save(replay, fmt::format("{}_replay", name));
		if (view_before.has_value()) save(*view_before, fmt::format("{}_view_before", name));
		if (view_after.has_value()) save(*view_after, fmt::format("{}_view_after", name));
	}
	return result;
}

/**
 * Write images of a tile: its captured sprites drawn on their own, and the
 * same area as the 2D view draws it, with the neighbouring tiles.
 * @param tile The tile.
 * @param name Name of the images in the screenshot directory: \a name and \a name with "_2d" appended.
 * @return Paths of the images that were written.
 * @pre The game state lock is held.
 */
std::vector<std::string> SaveTileCaptureImages(TileIndex tile, std::string_view name)
{
	TileSpriteCapture capture;
	CaptureTileSprites(tile, capture);
	const TileSprites &sprites = capture.tiles.front();
	const Point reference = RemapCoords(sprites.origin.x, sprites.origin.y, sprites.origin.z);

	/* Bounds of all sprites of the tile. */
	int left = INT_MAX, top = INT_MAX, right = INT_MIN, bottom = INT_MIN;
	auto extend = [&](const CapturedSprite &sprite) {
		const Sprite *spr = GetSprite(GB(sprite.image, 0, SPRITE_WIDTH), SpriteType::Normal);
		int x = reference.x + sprite.offset.x + spr->x_offs;
		int y = reference.y + sprite.offset.y + spr->y_offs;
		left = std::min(left, x);
		top = std::min(top, y);
		right = std::max(right, x + spr->width);
		bottom = std::max(bottom, y + spr->height);
	};
	for (const CapturedSprite &sprite : capture.ground) extend(sprite);
	for (const CapturedParentSprite &parent : capture.parents) extend(parent.sprite);
	for (const CapturedSprite &sprite : capture.children) extend(sprite);
	if (left > right) return {};

	const int padding = 8 * ZOOM_BASE;
	CaptureCanvas alone(left - padding, top - padding, right + padding, bottom + padding, ZoomLevel::Normal);
	CaptureCanvas in_view = alone;
	alone.Draw([&capture](const DrawPixelInfo &area) { DrawCapturedTileSprites(capture, area); });
	in_view.Draw([](const DrawPixelInfo &area) { DrawLandscapeLikeViewport(area); });

	std::vector<std::string> files;
	for (auto [canvas, file] : {std::pair{&alone, std::string(name)}, std::pair{&in_view, fmt::format("{}_2d", name)}}) {
		std::string path = ImagePath(file);
		if (canvas->SavePng(path)) files.push_back(std::move(path));
	}
	return files;
}

/**
 * Measure what capturing costs, per chunk of 16 x 16 tiles: the terrain as the
 * World Mirror captured it before, and the sprites on top.
 * @param chunk_x X coordinate of the chunk in the middle.
 * @param chunk_y Y coordinate of the chunk in the middle.
 * @param radius Number of chunks around it to capture as well.
 * @return The measurements.
 * @pre The game state lock is held.
 */
CaptureBenchmark BenchmarkChunkCapture(uint chunk_x, uint chunk_y, uint radius)
{
	using Clock = std::chrono::steady_clock;
	auto microseconds = [](Clock::duration duration) { return std::chrono::duration<double, std::micro>(duration).count(); };

	const ChunkGrid grid(Map::SizeX(), Map::SizeY());
	std::vector<std::pair<uint, uint>> chunks;
	for (uint cy = chunk_y - std::min(chunk_y, radius); cy <= std::min(chunk_y + radius, grid.size_y - 1); cy++) {
		for (uint cx = chunk_x - std::min(chunk_x, radius); cx <= std::min(chunk_x + radius, grid.size_x - 1); cx++) chunks.emplace_back(cx, cy);
	}

	CaptureBenchmark result;
	result.chunks = static_cast<uint>(chunks.size());
	if (chunks.empty()) return result;

	TerrainChunkData terrain;
	TileSpriteCapture sprites;
	std::vector<uint64_t> first_hashes;
	Clock::duration terrain_time{}, first_time{}, second_time{}, hash_time{}, slowest{};
	uint64_t checksum = 0;
	for (auto [cx, cy] : chunks) {
		auto t0 = Clock::now();
		CaptureTerrainChunk(cx, cy, terrain);
		checksum ^= HashTerrainChunk(terrain);
		auto t1 = Clock::now();
		CaptureChunkSprites(cx, cy, sprites);
		auto t2 = Clock::now();
		terrain_time += t1 - t0;
		first_time += t2 - t1;
		first_hashes.push_back(HashTileSprites(sprites));
	}
	for (size_t i = 0; i < chunks.size(); i++) {
		auto t0 = Clock::now();
		CaptureChunkSprites(chunks[i].first, chunks[i].second, sprites);
		auto t1 = Clock::now();
		uint64_t hash = HashTileSprites(sprites);
		auto t2 = Clock::now();
		second_time += t1 - t0;
		hash_time += t2 - t1;
		slowest = std::max(slowest, t1 - t0);
		result.tiles += sprites.tiles.size();
		result.sprites += sprites.SpriteCount();
		/* Nothing happens between both passes, so a capture must come out the same every time. */
		if (hash != first_hashes[i]) result.unstable_chunks++;
	}
	Debug(Facility::Fp, Severity::Debug1, "Capture benchmark checksum {:016x}", checksum);

	const double count = static_cast<double>(chunks.size());
	result.terrain_us = microseconds(terrain_time) / count;
	result.first_sprites_us = microseconds(first_time) / count;
	result.sprites_us = microseconds(second_time) / count;
	result.max_sprites_us = microseconds(slowest);
	result.hash_us = microseconds(hash_time) / count;
	return result;
}

} // namespace fp
