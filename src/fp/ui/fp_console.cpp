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
#include "../../console_func.h"
#include "../../console_internal.h"
#include "../../core/string_consumer.hpp"
#include "../../map_func.h"
#include "../../network/network.h"
#include "../../openttd.h"

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

		const SplineMirror &splines = runtime.GetSplines();
		const SplineMirror::Statistics &spline_stats = splines.GetStatistics();
		const SplineGraph::Statistics &graph = splines.GetGraph().GetStatistics();
		IConsolePrint(CC_DEFAULT, "  spline graph: {} chunks, {} pending; pieces: {} rail, {} road, {} tram, {} sidewalk; {} ports; debug overlay {}",
				spline_stats.built_chunks, spline_stats.pending_chunks,
				graph.pieces[to_underlying(SplineLayer::Rail)], graph.pieces[to_underlying(SplineLayer::Road)],
				graph.pieces[to_underlying(SplineLayer::Tram)], graph.pieces[to_underlying(SplineLayer::Sidewalk)],
				graph.ports, splines.HasOverlay() ? "on" : "off");
	}

	RendererStatus status = runtime.GetHandoff().GetRendererStatus();
	if (status.failed) {
		IConsolePrint(CC_ERROR, "Renderer: failed: {}", status.info);
	} else if (status.running) {
		IConsolePrint(CC_INFO, "Renderer: {}", status.info);
		IConsolePrint(CC_DEFAULT, "  {} chunks on the GPU, {} overlay chunks, {} vehicles drawn, {:.2f} ms per frame", status.chunks, status.overlay_chunks, status.vehicles, status.frame_ms);
	} else {
		IConsolePrint(CC_INFO, "Renderer: not running (needs an OpenGL video driver and an open first-person view).");
	}
}

/** Compare the spline graph with the simulation and print the result. */
static void PrintSplineCheck()
{
	Runtime &runtime = Runtime::Get();
	if (!runtime.IsActive()) {
		IConsolePrint(CC_ERROR, "The spline graph is only built while the first-person view is open.");
		return;
	}

	const SplineMirror &splines = runtime.GetSplines();
	const SplineMirror::Statistics &stats = splines.GetStatistics();
	IConsolePrint(CC_INFO, "Spline graph: {} chunks, {} pending, {} captured, {} changes without effect on tracks and roads, {} pieces fitted.",
			stats.built_chunks, stats.pending_chunks, stats.captured_chunks, stats.unchanged_chunks, splines.GetGraph().GetStatistics().refits);

	auto start = std::chrono::steady_clock::now();
	SplineCheck check = splines.Check();
	double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	IConsolePrint(CC_DEFAULT, "  {} pieces and {} joints checked in {:.1f} ms.", check.pieces, check.joints, ms);
	IConsolePrint(CC_DEFAULT, "  joints: largest gap {:.2e} m, largest kink {:.2e} rad", check.max_gap, check.max_kink);
	if (check.crossing_joints > 0) {
		IConsolePrint(CC_DEFAULT, "  {} joints at crossovers: largest kink {:.1f} degrees, where a line crosses another branch", check.crossing_joints, check.max_crossing_kink * 180 / std::numbers::pi);
	}
	IConsolePrint(CC_DEFAULT, "  surface model vs. GetSlopePixelZ(): largest difference {:.2f} units", check.max_surface_error);
	IConsolePrint(CC_DEFAULT, "  curves vs. straight chords: rail up to {:.2f} m aside, roads up to {:.2f} m (they turn around the corner)",
			check.max_chord_distance[to_underlying(SplineLayer::Rail)], check.max_chord_distance[to_underlying(SplineLayer::Road)]);
	IConsolePrint(CC_DEFAULT, "  vertical profiles up to {:.2f} m above or below the surface, because of the blends between grades", check.max_profile_offset);
	if (std::isfinite(check.min_rail_radius)) {
		IConsolePrint(CC_DEFAULT, "  tightest rail curve: radius {:.1f} m on tile {}x{}", check.min_rail_radius, check.tightest_rail.x, check.tightest_rail.y);
	}
}

/**
 * Control the first-person mode.
 * @param argv Arguments of the command.
 * @return True iff the command was valid.
 */
static bool ConFirstPerson(std::span<std::string_view> argv)
{
	if (argv.empty()) {
		IConsolePrint(CC_HELP, "Control the first-person mode. Usage: 'fp [view|status|look|splines|debug_splines [on|off]]'.");
		IConsolePrint(CC_HELP, "  view:          open the first-person view (default).");
		IConsolePrint(CC_HELP, "  status:        show the state of the sim bridge, the world mirror and the renderer.");
		IConsolePrint(CC_HELP, "  look <x> <y> [distance] [height] [heading]: put the camera behind and above a tile, looking at it;");
		IConsolePrint(CC_HELP, "                 metres and degrees, heading 0 looks south-west along X, 90 south-east along Y.");
		IConsolePrint(CC_HELP, "  splines:       check the spline graph of tracks and roads against the simulation.");
		IConsolePrint(CC_HELP, "  debug_splines: show or hide the spline graph in the first-person view.");
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
	if (action == "look") {
		Runtime &runtime = Runtime::Get();
		if (!runtime.IsActive() || argv.size() < 4) return false;
		auto tile_x = ParseInteger<uint>(argv[2]);
		auto tile_y = ParseInteger<uint>(argv[3]);
		if (!tile_x.has_value() || !tile_y.has_value() || *tile_x >= Map::SizeX() || *tile_y >= Map::SizeY()) return false;
		std::array<int, 3> view{120, 60, 225};
		for (size_t i = 0; i < view.size() && 4 + i < argv.size(); i++) {
			auto value = ParseInteger<int>(argv[4 + i]);
			if (!value.has_value()) return false;
			view[i] = *value;
		}
		runtime.LookAt(*tile_x * TILE_SIZE + TILE_SIZE / 2, *tile_y * TILE_SIZE + TILE_SIZE / 2, view[0], view[1], view[2] * std::numbers::pi / 180.0);
		return true;
	}
	if (action == "splines") {
		PrintSplineCheck();
		return true;
	}
	if (action == "debug_splines") {
		Runtime &runtime = Runtime::Get();
		bool enabled = !runtime.GetSplines().HasOverlay();
		if (argv.size() > 2) {
			if (argv[2] == "on") {
				enabled = true;
			} else if (argv[2] == "off") {
				enabled = false;
			} else {
				return false;
			}
		}
		runtime.SetDebugSplines(enabled);
		IConsolePrint(CC_INFO, "Spline graph overlay {}.", enabled ? "on" : "off");
		return true;
	}
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
