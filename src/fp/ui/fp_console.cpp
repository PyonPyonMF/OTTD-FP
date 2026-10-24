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
 * Control the first-person mode.
 * @param argv Arguments of the command.
 * @return True iff the command was valid.
 */
static bool ConFirstPerson(std::span<std::string_view> argv)
{
	if (argv.empty()) {
		IConsolePrint(CC_HELP, "Control the first-person mode. Usage: 'fp [view|status]'.");
		IConsolePrint(CC_HELP, "  view:   open the first-person view (default).");
		IConsolePrint(CC_HELP, "  status: show the state of the sim bridge, the world mirror and the renderer.");
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
