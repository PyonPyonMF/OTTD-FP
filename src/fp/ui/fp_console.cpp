/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_console.cpp Console commands of the first-person mode. */

#include "../../stdafx.h"
#include "fp_window.h"
#include "../fp_hooks.h"
#include "../runtime/fp_runtime.h"
#include "../sim/fp_bridge.h"
#include "../world/fp_capture_check.h"
#include "../../console_func.h"
#include "../../console_internal.h"
#include "../../map_func.h"
#include "../../bridge_map.h"
#include "../../core/string_consumer.hpp"
#include "../../network/network.h"
#include "../../openttd.h"
#include "../../tile_map.h"
#include "../../tilearea_spiral.h"

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
		IConsolePrint(CC_DEFAULT, "  world mirror: {} chunks built, {} pending, {} meshed, {} with new sprites, {} changes without effect",
				mirror.built_chunks, mirror.pending_chunks, mirror.meshed_chunks, mirror.sprite_changes, mirror.unchanged_chunks);
		if (mirror.captured_chunks > 0) {
			auto per_chunk = [&mirror](std::chrono::nanoseconds time) { return std::chrono::duration<double, std::micro>(time).count() / mirror.captured_chunks; };
			IConsolePrint(CC_DEFAULT, "  per chunk: terrain {:.0f} us, sprites {:.0f} us ({:.0f} sprites)",
					per_chunk(mirror.terrain_time), per_chunk(mirror.sprite_time), static_cast<double>(mirror.captured_sprites) / mirror.captured_chunks);
		}
	}

	RendererStatus status = runtime.GetHandoff().GetRendererStatus();
	if (status.failed) {
		IConsolePrint(CC_ERROR, "Renderer: failed: {}", status.info);
	} else if (status.running) {
		IConsolePrint(CC_INFO, "Renderer: {}", status.info);
		IConsolePrint(CC_DEFAULT, "  {} chunks on the GPU, {} vehicles drawn, {:.2f} ms per frame", status.chunks, status.vehicles, status.frame_ms);
	} else {
		IConsolePrint(CC_INFO, "Renderer: not running (needs an OpenGL video driver and an open first-person view).");
	}
}

/**
 * Parse a tile coordinate.
 * @param arg The argument.
 * @param size Size of the map along the axis.
 * @return The coordinate, if valid.
 */
static std::optional<uint> ParseTileCoordinate(std::string_view arg, uint size)
{
	auto value = ParseInteger<uint>(arg, 0);
	if (!value.has_value() || *value >= size) return std::nullopt;
	return value;
}

/**
 * The tile at the centre of the main viewport.
 * @return The tile.
 */
static TileIndex MainViewportTile()
{
	Point centre = MainViewportCentre();
	return TileVirtXY(Clamp<int>(centre.x, 0, Map::MaxX() * TILE_SIZE), Clamp<int>(centre.y, 0, Map::MaxY() * TILE_SIZE));
}

/**
 * 'fp capture <x> <y> [name]': list the sprites the 2D view draws for a tile.
 * @param argv Arguments of the command, starting with 'fp capture'.
 * @return True iff the arguments were valid.
 */
static bool ConCapture(std::span<std::string_view> argv)
{
	if (argv.size() < 4 || argv.size() > 5) return false;
	auto x = ParseTileCoordinate(argv[2], Map::SizeX());
	auto y = ParseTileCoordinate(argv[3], Map::SizeY());
	if (!x.has_value() || !y.has_value()) {
		IConsolePrint(CC_ERROR, "Tile coordinates must lie within the map of {} x {} tiles.", Map::SizeX(), Map::SizeY());
		return true;
	}

	TileSpriteCapture capture;
	CaptureTileSprites(TileXY(*x, *y), capture);
	for (const std::string &line : DescribeTileSprites(capture, capture.tiles.front())) IConsolePrint(CC_DEFAULT, "{}", line);

	if (argv.size() == 5) {
		for (const std::string &file : SaveTileCaptureImages(TileXY(*x, *y), argv[4])) IConsolePrint(CC_INFO, "Wrote {}", file);
	}
	return true;
}

/**
 * 'fp capture_find [radius]': find a tile of every kind near the centre of the 2D view, to capture.
 * @param argv Arguments of the command, starting with 'fp capture_find'.
 * @return True iff the arguments were valid.
 */
static bool ConCaptureFind(std::span<std::string_view> argv)
{
	if (argv.size() > 3) return false;
	uint radius = 32;
	if (argv.size() > 2) {
		auto value = ParseInteger<uint>(argv[2]);
		if (!value.has_value() || *value > 1024) return false;
		radius = *value;
	}

	/* Kinds of tiles, in the order of the acceptance criteria of G2. */
	static const std::array<std::pair<std::string_view, bool (*)(TileIndex)>, 10> kinds = {{
		{"rail", [](TileIndex t) { return IsTileType(t, TileType::Railway); }},
		{"road", [](TileIndex t) { return IsTileType(t, TileType::Road); }},
		{"house", [](TileIndex t) { return IsTileType(t, TileType::House); }},
		{"station", [](TileIndex t) { return IsTileType(t, TileType::Station); }},
		{"industry", [](TileIndex t) { return IsTileType(t, TileType::Industry); }},
		{"object", [](TileIndex t) { return IsTileType(t, TileType::Object); }},
		{"bridge head", [](TileIndex t) { return IsTileType(t, TileType::TunnelBridge) && IsBridge(t); }},
		{"under a bridge", [](TileIndex t) { return IsTileType(t, TileType::Void) ? false : IsBridgeAbove(t); }},
		{"water", [](TileIndex t) { return IsTileType(t, TileType::Water); }},
		{"trees", [](TileIndex t) { return IsTileType(t, TileType::Trees); }},
	}};
	std::array<TileIndex, kinds.size()> found;
	found.fill(INVALID_TILE);

	TileIndex centre = MainViewportTile();
	for (TileIndex tile : SpiralTileSequence(centre, 2 * radius + 1)) {
		for (size_t i = 0; i < kinds.size(); i++) {
			if (found[i] == INVALID_TILE && kinds[i].second(tile)) found[i] = tile;
		}
	}

	IConsolePrint(CC_INFO, "Nearest tiles to {} x {}:", TileX(centre), TileY(centre));
	for (size_t i = 0; i < kinds.size(); i++) {
		if (found[i] == INVALID_TILE) {
			IConsolePrint(CC_DEFAULT, "  {}: none within {} tiles", kinds[i].first, radius);
		} else {
			IConsolePrint(CC_DEFAULT, "  {}: fp capture {} {}", kinds[i].first, TileX(found[i]), TileY(found[i]));
		}
	}
	return true;
}

/**
 * 'fp capture_check [radius] [name]': prove that captures hold everything the 2D view draws.
 * @param argv Arguments of the command, starting with 'fp capture_check'.
 * @return True iff the arguments were valid.
 */
static bool ConCaptureCheck(std::span<std::string_view> argv)
{
	if (argv.size() > 4) return false;
	uint radius = 8;
	if (argv.size() > 2) {
		auto value = ParseInteger<uint>(argv[2]);
		if (!value.has_value() || *value > 64) return false;
		radius = *value;
	}
	std::string_view name = argv.size() > 3 ? argv[3] : std::string_view{};

	TileIndex centre = MainViewportTile();
	CaptureCheckResult result = CheckTileCapture(centre, radius, name);
	IConsolePrint(CC_INFO, "Captured {} tiles around {} x {} with {} sprites in {:.1f} ms.", result.tiles, TileX(centre), TileY(centre), result.sprites, result.capture_ms);
	IConsolePrint(result.replay_differences == 0 ? CC_INFO : CC_ERROR, "  captured sprites drawn again: {} of {} x {} pixels differ from the 2D view.",
			result.replay_differences, result.width, result.height);
	if (result.view_width > 0) {
		IConsolePrint(result.view_differences == 0 ? CC_INFO : CC_ERROR, "  main viewport after capturing: {} of {} x {} pixels differ from before.",
				result.view_differences, result.view_width, result.view_height);
	}
	if (result.beyond_map) IConsolePrint(CC_WARNING, "  The area reaches beyond the map; differences in the black void there are expected.");
	for (const std::string &file : result.files) IConsolePrint(CC_INFO, "Wrote {}", file);
	return true;
}

/**
 * 'fp capture_bench [radius]': measure the cost of capturing chunks.
 * @param argv Arguments of the command, starting with 'fp capture_bench'.
 * @return True iff the arguments were valid.
 */
static bool ConCaptureBench(std::span<std::string_view> argv)
{
	if (argv.size() > 3) return false;
	uint radius = 2;
	if (argv.size() > 2) {
		auto value = ParseInteger<uint>(argv[2]);
		if (!value.has_value() || *value > 64) return false;
		radius = *value;
	}

	TileIndex centre = MainViewportTile();
	CaptureBenchmark result = BenchmarkChunkCapture(TileX(centre) / CHUNK_SIZE, TileY(centre) / CHUNK_SIZE, radius);
	if (result.chunks == 0) return true;
	IConsolePrint(CC_INFO, "Captured {} chunks of {} x {} tiles, {:.0f} sprites per chunk.", result.chunks, CHUNK_SIZE, CHUNK_SIZE, static_cast<double>(result.sprites) / result.chunks);
	IConsolePrint(CC_DEFAULT, "  terrain: {:.1f} us per chunk", result.terrain_us);
	IConsolePrint(CC_DEFAULT, "  sprites: {:.1f} us per chunk on first capture, {:.1f} us again (slowest {:.1f} us), {:.1f} us per tile",
			result.first_sprites_us, result.sprites_us, result.max_sprites_us, result.sprites_us * result.chunks / std::max<size_t>(result.tiles, 1));
	IConsolePrint(CC_DEFAULT, "  hash of the sprites: {:.1f} us per chunk", result.hash_us);
	IConsolePrint(result.unstable_chunks == 0 ? CC_INFO : CC_ERROR, "  {} of {} chunks captured differently the second time.", result.unstable_chunks, result.chunks);
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
		IConsolePrint(CC_HELP, "Control the first-person mode. Usage: 'fp [view|status|capture|capture_find|capture_check|capture_bench]'.");
		IConsolePrint(CC_HELP, "  view:   open the first-person view (default).");
		IConsolePrint(CC_HELP, "  status: show the state of the sim bridge, the world mirror and the renderer.");
		IConsolePrint(CC_HELP, "  capture <x> <y> [name]: list the sprites the 2D view draws for a tile; with a name, also write images of it.");
		IConsolePrint(CC_HELP, "  capture_find [radius]: find a tile of every kind near the centre of the 2D view.");
		IConsolePrint(CC_HELP, "  capture_check [radius] [name]: check pixel by pixel that captured sprites draw like the 2D view, around its centre.");
		IConsolePrint(CC_HELP, "  capture_bench [radius]: measure capturing the chunks around the centre of the 2D view.");
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
	if (action == "capture") return ConCapture(argv);
	if (action == "capture_find") return ConCaptureFind(argv);
	if (action == "capture_check") return ConCaptureCheck(argv);
	if (action == "capture_bench") return ConCaptureBench(argv);
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
