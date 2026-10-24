/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_console.cpp Console commands of the first-person mode. */

#include "../../stdafx.h"

#include <filesystem>

#include "fp_window.h"
#include "../fp_hooks.h"
#include "../runtime/fp_runtime.h"
#include "../sim/fp_bridge.h"
#include "../sprites/fp_sprite_bridge.h"
#include "../sprites/fp_sprite_decode.h"
#include "../sprites/fp_sprite_png.h"
#include "../../console_func.h"
#include "../../console_internal.h"
#include "../../core/string_consumer.hpp"
#include "../../fileio_func.h"
#include "../../map_func.h"
#include "../../network/network.h"
#include "../../openttd.h"
#include "../../palette_func.h"
#include "../../sprite.h"
#include "../../spritecache.h"
#include "../../vehicle_base.h"
#include "../../vehicle_func.h"
#include "../../table/sprites.h"

#include "../../safeguards.h"

namespace fp {

/** Print the state of the first-person mode. */
static void PrintStatus()
{
	const Bridge &bridge = Bridge::Get();
	const Bridge::Statistics &bridge_stats = bridge.GetStatistics();
	IConsolePrint(CC_INFO, "Sim bridge: {}, session {}, world epoch {}.", bridge.IsActive() ? "active" : "inactive", bridge.GetSession(), bridge.GetWorldEpoch());
	IConsolePrint(CC_DEFAULT, "  snapshots published: {}, vehicles in last snapshot: {}, tile changes recorded: {}",
			bridge_stats.snapshots_published, bridge_stats.last_vehicle_count, bridge_stats.tiles_marked);

	Runtime &runtime = Runtime::Get();
	const MetricScale &scale = runtime.GetScale();
	IConsolePrint(CC_INFO, "Runtime: {}; {:.2f} m per tile, {:.1f} m per height level.", runtime.IsActive() ? "active" : "inactive", scale.TileLength(), scale.metres_per_level);
	if (runtime.IsActive()) {
		const Spectator &spectator = runtime.GetSpectator();
		const CameraPose camera = spectator.GetCamera();
		IConsolePrint(CC_DEFAULT, "  role: {}, camera at ({:.1f}, {:.1f}, {:.1f}) m, {:.1f} m above ground",
				spectator.GetName(), camera.position.x, camera.position.y, camera.position.z, spectator.GetHeightAboveGround());

		const WorldMirror::Statistics &mirror = runtime.GetWorldMirror().GetStatistics();
		IConsolePrint(CC_DEFAULT, "  world mirror: {} chunks built, {} pending, {} meshed, {} changes without effect on the terrain",
				mirror.built_chunks, mirror.pending_chunks, mirror.meshed_chunks, mirror.unchanged_chunks);
	}

	SpriteBridge &sprites = SpriteBridge::Get();
	const SpriteBridge::Statistics &sprite_stats = sprites.GetStatistics();
	IConsolePrint(CC_INFO, "Sprite bridge: sprite generation {}; {} sprites read, {} images requested, {} from the cache, {} invalid.",
			sprites.GetGeneration(), sprite_stats.sprites_read, sprite_stats.requests, sprite_stats.image_hits, sprite_stats.invalid_requests);
	IConsolePrint(CC_DEFAULT, "  cached: {} sprites in {} KiB, {} images in {} KiB; dropped {} times for reloaded sprites",
			sprites.GetRawCache().GetCount(), sprites.GetRawCache().GetSize() / 1024, sprites.GetImageCache().GetCount(), sprites.GetImageCache().GetSize() / 1024, sprite_stats.clears);
	if (runtime.IsActive()) {
		const SpriteAtlas::Statistics atlas = runtime.GetSpriteAtlas().GetStatistics();
		IConsolePrint(CC_DEFAULT, "  atlas: {} images on {} pages of {}x{}, {:.1f}% of the pages used, epoch {}, {} KiB uploaded; {} sprites shown",
				atlas.images, atlas.pages, ATLAS_PAGE_SIZE, ATLAS_PAGE_SIZE,
				atlas.pages == 0 ? 0.0 : 100.0 * atlas.used_area / (static_cast<double>(atlas.pages) * ATLAS_PAGE_SIZE * ATLAS_PAGE_SIZE),
				atlas.epoch, atlas.uploaded_bytes / 1024, runtime.GetShownSpriteCount());
	}

	RendererStatus status = runtime.GetHandoff().GetRendererStatus();
	if (status.failed) {
		IConsolePrint(CC_ERROR, "Renderer: failed: {}", status.info);
	} else if (status.running) {
		IConsolePrint(CC_INFO, "Renderer: {}", status.info);
		IConsolePrint(CC_DEFAULT, "  {} chunks on the GPU, {} vehicles drawn, {} billboards drawn, {} atlas pages, {:.2f} ms per frame",
				status.chunks, status.vehicles, status.billboards, status.atlas_pages, status.frame_ms);
	} else {
		IConsolePrint(CC_INFO, "Renderer: not running (needs an OpenGL video driver and an open first-person view).");
	}
}

/**
 * Read a palette argument.
 * @param arg The argument: a number, 'none', 'crash', 'black' or 'company:<colour>' with a company colour from 0 to 15.
 * @return The palette, or std::nullopt if the argument is none of these.
 */
static std::optional<PaletteID> ParsePalette(std::string_view arg)
{
	if (arg == "none") return PAL_NONE;
	if (arg == "crash") return PALETTE_CRASH;
	if (arg == "black") return PALETTE_ALL_BLACK;
	if (arg.starts_with("company:")) {
		auto colour = ParseInteger<uint8_t>(arg.substr(8));
		if (!colour.has_value() || *colour >= to_underlying(Colours::End)) return std::nullopt;
		return GetColourPalette(static_cast<Colours>(*colour));
	}
	return ParseInteger<PaletteID>(arg, 0);
}

/**
 * The layers of a vehicle as the 2D view draws them, see DoDrawVehicle().
 * @param id The vehicle.
 * @param[out] layers Receives the sprites with their palettes.
 * @return Why there are none, or std::nullopt on success.
 */
static std::optional<std::string> GetVehicleLayers(std::string_view id, std::vector<PalSpriteID> &layers)
{
	auto index = ParseInteger<uint32_t>(id);
	const Vehicle *v = index.has_value() ? Vehicle::GetIfValid(*index) : nullptr;
	if (v == nullptr) return fmt::format("'{}' is not a vehicle", id);

	VehicleSpriteSeq seq = v->sprite_cache.sprite_seq;
	/* The 2D view refreshes the sprite of a vehicle only when it comes into view. */
	if (v->sprite_cache.revalidate_before_draw) {
		VehicleSpriteSeq fresh;
		v->GetImage(v->direction, EngineImageType::OnMap, &fresh);
		if (fresh.IsValid()) seq = fresh;
	}
	if (!seq.IsValid()) return fmt::format("vehicle {} has no sprite", *index);

	PaletteID palette = PAL_NONE;
	const bool crashed = v->vehstatus.Test(VehState::Crashed);
	if (v->vehstatus.Test(VehState::DefaultPalette)) palette = crashed ? PALETTE_CRASH : GetVehiclePalette(v);

	layers.clear();
	for (uint i = 0; i < seq.count; i++) {
		PaletteID layer_palette = (seq.seq[i].pal == PAL_NONE || crashed) ? palette : seq.seq[i].pal;
		layers.push_back({seq.seq[i].sprite, layer_palette});
	}
	return std::nullopt;
}

/**
 * Read the sprite arguments of the sprite commands: either '<sprite> [palette]' or 'vehicle <id>'.
 * @param args The arguments.
 * @param[out] layers Receives the sprites with their palettes.
 * @return Why the arguments are no sprite, or std::nullopt on success.
 */
static std::optional<std::string> ParseSpriteArguments(std::span<const std::string_view> args, std::vector<PalSpriteID> &layers)
{
	if (args.empty()) return "a sprite is missing";
	if (args[0] == "vehicle") {
		if (args.size() != 2) return "'vehicle' needs exactly one vehicle id";
		return GetVehicleLayers(args[1], layers);
	}

	if (args.size() > 2) return "too many arguments";
	auto sprite = ParseInteger<SpriteID>(args[0], 0);
	if (!sprite.has_value()) return fmt::format("'{}' is not a sprite number", args[0]);
	PaletteID palette = PAL_NONE;
	if (args.size() == 2) {
		auto parsed = ParsePalette(args[1]);
		if (!parsed.has_value()) return fmt::format("'{}' is not a palette", args[1]);
		palette = *parsed;
	}
	layers = {{*sprite, palette}};
	return std::nullopt;
}

/**
 * Name of a zoom level of sprite pixels.
 * @param zoom The zoom level.
 * @return Its resolution relative to the original graphics.
 */
static std::string_view ZoomName(ZoomLevel zoom)
{
	switch (zoom) {
		case ZoomLevel::Min: return "4x";
		case ZoomLevel::In2x: return "2x";
		case ZoomLevel::Normal: return "1x";
		default: return "below 1x";
	}
}

/**
 * Decode the layers of a sprite and combine them into one image.
 * @param layers The sprites with their palettes.
 * @param[out] image Receives the image.
 * @return Why decoding failed, or std::nullopt on success.
 */
static std::optional<std::string> DecodeLayers(std::span<const PalSpriteID> layers, SpriteImage &image)
{
	SpriteBridge &sprites = SpriteBridge::Get();
	std::vector<std::shared_ptr<const SpriteImage>> images;
	for (const PalSpriteID &layer : layers) {
		if (auto problem = SpriteBridge::CheckRequest(layer.sprite, layer.pal); problem.has_value()) return problem;
		images.push_back(sprites.GetImage(layer.sprite, layer.pal));
	}

	if (images.size() == 1) {
		image = *images[0];
		return std::nullopt;
	}
	std::vector<const SpriteImage *> pointers;
	for (const auto &layer : images) pointers.push_back(layer.get());
	ComposeImages(pointers, image);
	return std::nullopt;
}

/**
 * Print what is known about a sprite: 'fp sprite_info'.
 * @param args The sprite arguments.
 * @return True iff the arguments were valid.
 */
static bool ConSpriteInfo(std::span<const std::string_view> args)
{
	std::vector<PalSpriteID> layers;
	if (auto problem = ParseSpriteArguments(args, layers); problem.has_value()) {
		IConsolePrint(CC_ERROR, "{}.", *problem);
		return false;
	}

	if (args[0] == "vehicle") {
		static const std::array<std::string_view, 6> type_names = {"train", "road vehicle", "ship", "aircraft", "effect", "disaster"};
		const Vehicle *v = Vehicle::Get(*ParseInteger<uint32_t>(args[1]));
		IConsolePrint(CC_INFO, "Vehicle {}: {}{}, company {}, {} layers.", v->index.base(), to_underlying(v->type) < type_names.size() ? type_names[to_underlying(v->type)] : "?",
				v->IsFrontEngine() ? ", front engine" : "", v->owner.base(), layers.size());
	}

	SpriteBridge &sprites = SpriteBridge::Get();
	for (const PalSpriteID &layer : layers) {
		if (auto problem = SpriteBridge::CheckRequest(layer.sprite, layer.pal); problem.has_value()) {
			IConsolePrint(CC_ERROR, "Sprite {} with palette {}: {}.", layer.sprite, layer.pal, *problem);
			continue;
		}

		SpriteID sprite = layer.sprite;
		PaletteID palette = layer.pal;
		NormaliseSpriteRequest(sprite, palette);
		auto raw = sprites.GetRawSprite(sprite);
		auto image = sprites.GetImage(sprite, palette);
		std::string_view depth = !raw->colours.Test(SpriteComponent::RGB) ? "8bpp" : raw->colours.Test(SpriteComponent::Palette) ? "32bpp with recolour mask" : "32bpp";
		const SpriteFile *file = GetOriginFile(sprite);
		size_t visible = std::count_if(image->pixels.begin(), image->pixels.end(), [](const Rgba8 &p) { return p.a != 0; });
		IConsolePrint(CC_INFO, "Sprite {} with palette {}: {}x{} pixels at {}, offsets ({}, {}), {}, {} visible pixels; from '{}', local id {}.",
				sprite, palette, image->width, image->height, ZoomName(image->zoom), image->x_offs, image->y_offs, depth, visible,
				file != nullptr ? file->GetSimplifiedFilename() : "?", GetSpriteLocalID(sprite));
	}
	return true;
}

/**
 * Write a sprite to a PNG file: 'fp sprite_dump'.
 * @param args The sprite arguments followed by the file name.
 * @return True iff the arguments were valid.
 */
static bool ConSpriteDump(std::span<const std::string_view> args)
{
	if (args.size() < 2) {
		IConsolePrint(CC_ERROR, "A sprite and a file name are needed.");
		return false;
	}

	std::vector<PalSpriteID> layers;
	if (auto problem = ParseSpriteArguments(args.first(args.size() - 1), layers); problem.has_value()) {
		IConsolePrint(CC_ERROR, "{}.", *problem);
		return false;
	}

	SpriteImage image;
	if (auto problem = DecodeLayers(layers, image); problem.has_value()) {
		IConsolePrint(CC_ERROR, "Cannot decode: {}.", *problem);
		return true;
	}

	/* Relative names go to the screenshot folder, like screenshots do. */
	std::string filename{args.back()};
	if (!std::filesystem::path(filename).is_absolute()) filename = fmt::format("{}{}", FiosGetScreenshotDir(), filename);

	if (auto problem = WriteSpritePng(image, filename); problem.has_value()) {
		IConsolePrint(CC_ERROR, "Cannot write the sprite: {}.", *problem);
		return true;
	}
	IConsolePrint(CC_INFO, "Wrote {}x{} pixels at {}, offsets ({}, {}), to '{}'.", image.width, image.height, ZoomName(image.zoom), image.x_offs, image.y_offs, filename);
	return true;
}

/**
 * Show a sprite as billboard in the first-person view: 'fp sprite_show'.
 * @param args The sprite arguments, or 'clear'.
 * @return True iff the arguments were valid.
 */
static bool ConSpriteShow(std::span<const std::string_view> args)
{
	Runtime &runtime = Runtime::Get();
	if (args.size() == 1 && args[0] == "clear") {
		runtime.ClearShownSprites();
		IConsolePrint(CC_INFO, "Removed all shown sprites.");
		return true;
	}

	std::vector<PalSpriteID> layers;
	if (auto problem = ParseSpriteArguments(args, layers); problem.has_value()) {
		IConsolePrint(CC_ERROR, "{}.", *problem);
		return false;
	}
	for (const PalSpriteID &layer : layers) {
		if (auto problem = SpriteBridge::CheckRequest(layer.sprite, layer.pal); problem.has_value()) {
			IConsolePrint(CC_ERROR, "Cannot show sprite {}: {}.", layer.sprite, *problem);
			return true;
		}
	}
	if (!runtime.IsActive()) {
		IConsolePrint(CC_ERROR, "Open the first-person view first, with 'fp view'.");
		return true;
	}

	runtime.ShowSprite(layers);
	IConsolePrint(CC_INFO, "Showing {} in front of the camera; 'fp sprite_show clear' removes it.", layers.size() == 1 ? fmt::format("sprite {}", layers[0].sprite) : fmt::format("{} layers", layers.size()));
	return true;
}

/**
 * Read a decimal number like "-12.5", independent of the locale.
 * @param arg The argument.
 * @return The number, or std::nullopt if the argument is none.
 */
static std::optional<double> ParseDecimal(std::string_view arg)
{
	bool negative = arg.starts_with('-');
	if (negative) arg.remove_prefix(1);
	size_t dot = arg.find('.');
	std::string_view whole = arg.substr(0, dot);
	std::string_view fraction = dot == std::string_view::npos ? std::string_view{} : arg.substr(dot + 1);
	if ((whole.empty() && fraction.empty()) || fraction.size() > 9) return std::nullopt;

	double value = 0;
	if (!whole.empty()) {
		auto parsed = ParseInteger<uint32_t>(whole);
		if (!parsed.has_value()) return std::nullopt;
		value = *parsed;
	}
	if (!fraction.empty()) {
		auto parsed = ParseInteger<uint32_t>(fraction);
		if (!parsed.has_value()) return std::nullopt;
		value += *parsed / std::pow(10.0, static_cast<double>(fraction.size()));
	}
	return negative ? -value : value;
}

/**
 * Print or set the camera: 'fp camera'.
 * @param args Nothing, or the tile coordinates followed by height above the ground in metres, heading and pitch in degrees.
 * @return True iff the arguments were valid.
 */
static bool ConCamera(std::span<const std::string_view> args)
{
	Runtime &runtime = Runtime::Get();
	if (!runtime.IsActive()) {
		IConsolePrint(CC_ERROR, "Open the first-person view first, with 'fp view'.");
		return true;
	}

	const MetricScale &scale = runtime.GetScale();
	if (args.empty()) {
		const CameraPose camera = runtime.GetSpectator().GetCamera();
		IConsolePrint(CC_INFO, "Camera at tile ({:.2f}, {:.2f}), {:.1f} m above the ground, heading {:.1f}, pitch {:.1f} degrees.",
				camera.position.x / scale.TileLength(), camera.position.y / scale.TileLength(), runtime.GetSpectator().GetHeightAboveGround(),
				camera.yaw * 180 / std::numbers::pi, camera.pitch * 180 / std::numbers::pi);
		return true;
	}
	if (args.size() < 2 || args.size() > 5) return false;

	std::array<double, 5> values = {0, 0, Spectator::EYE_HEIGHT, 0, 0};
	for (size_t i = 0; i < args.size(); i++) {
		auto value = ParseDecimal(args[i]);
		if (!value.has_value()) {
			IConsolePrint(CC_ERROR, "'{}' is not a number.", args[i]);
			return false;
		}
		values[i] = *value;
	}
	runtime.PlaceCamera(values[0] * scale.TileLength(), values[1] * scale.TileLength(), values[2], values[3] * std::numbers::pi / 180, values[4] * std::numbers::pi / 180);
	return true;
}

/**
 * Control the first-person mode.
 * @param argv Arguments of the command.
 * @return True iff the command was valid.
 */
static bool ConFirstPerson(std::span<std::string_view> argv)
{
	if (argv.empty()) {
		IConsolePrint(CC_HELP, "Control the first-person mode. Usage: 'fp [view|status|camera|sprite_info|sprite_dump|sprite_show]'.");
		IConsolePrint(CC_HELP, "  view:   open the first-person view (default).");
		IConsolePrint(CC_HELP, "  status: show the state of the sim bridge, the world mirror, the sprites and the renderer.");
		IConsolePrint(CC_HELP, "  camera [<tile x> <tile y> [height] [heading] [pitch]]: print or set the camera; metres above the ground, degrees.");
		IConsolePrint(CC_HELP, "  sprite_info <sprite> [palette]: size, offsets, colour depth and origin of a sprite.");
		IConsolePrint(CC_HELP, "  sprite_dump <sprite> [palette] <file.png>: write a sprite as the 2D view draws it; relative names go to the screenshot folder.");
		IConsolePrint(CC_HELP, "  sprite_show <sprite> [palette]: show a sprite as billboard in front of the camera; 'sprite_show clear' removes all.");
		IConsolePrint(CC_HELP, "  Instead of '<sprite> [palette]' the sprite commands take 'vehicle <id>': the vehicle as the 2D view draws it.");
		IConsolePrint(CC_HELP, "  Palettes: a number, 'none', 'crash', 'black' or 'company:<colour>' with a company colour from 0 to 15.");
		return true;
	}

	std::string_view action = argv.size() > 1 ? argv[1] : "view";
	if (action == "view") {
		ShowFirstPersonWindow();
		return true;
	}
	if (action == "status") {
		PrintStatus();
		return true;
	}

	std::span<const std::string_view> args = argv.size() > 2 ? argv.subspan(2) : std::span<const std::string_view>{};
	if (action == "camera") return ConCamera(args);
	if (action == "sprite_info") return ConSpriteInfo(args);
	if (action == "sprite_dump") return ConSpriteDump(args);
	if (action == "sprite_show") return ConSpriteShow(args);
	return false;
}

/**
 * The first-person mode needs a map and a user interface.
 * @param echo Whether to print why the command is not available.
 * @return Whether the command may be used.
 */
static ConsoleHookResult ConHookFirstPerson(bool echo)
{
	if (_network_dedicated || _game_mode == GameMode::Bootstrap || Map::Size() == 0) {
		if (echo) IConsolePrint(CC_ERROR, "The first-person mode needs a map and a user interface.");
		return ConsoleHookResult::Disallow;
	}
	return ConsoleHookResult::Allow;
}

/* Hook called by the console; see fp_hooks.h. */

void RegisterConsoleCommands()
{
	IConsole::CmdRegister("fp", ConFirstPerson, ConHookFirstPerson);
}

} // namespace fp
