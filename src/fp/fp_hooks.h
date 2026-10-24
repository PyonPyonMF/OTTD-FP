/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_hooks.h Hook points through which the OpenTTD core notifies the first-person mode. */

#ifndef FP_HOOKS_H
#define FP_HOOKS_H

#include "../tile_type.h"
#include "../vehicle_type.h"

/**
 * The first-person mode (OTTD-FP), see docs/fp/architecture.md.
 *
 * The core calls the functions in this header at a handful of places, each
 * marked with a "FP hook" comment. They are the only coupling between the
 * core and the first-person mode, which keeps merges with upstream cheap.
 * Without WITH_FP every hook is an empty inline function.
 *
 * Unless stated otherwise, the hooks are called with the game state lock held.
 */
namespace fp {

#ifdef WITH_FP
void OnMapAllocated();
void OnTileDirty(TileIndex tile);
void OnTickEnd();
void OnVehicleDestroyed(VehicleID index);
void OnSpritesInvalidated();
void RegisterConsoleCommands();
bool CapturesDirectionKeys();
#else
inline void OnMapAllocated() {}
inline void OnTileDirty(TileIndex) {}
inline void OnTickEnd() {}
inline void OnVehicleDestroyed(VehicleID) {}
inline void OnSpritesInvalidated() {}
inline void RegisterConsoleCommands() {}
inline bool CapturesDirectionKeys() { return false; }
#endif /* WITH_FP */

#if defined(WITH_FP) && defined(WITH_OPENGL) && !defined(DEDICATED)
/* Called from the OpenGL back-end on the drawing thread, without the game state lock and with the OpenGL context current. */
void OnGLPaint();
void OnGLContextDestroy();
#else
inline void OnGLPaint() {}
inline void OnGLContextDestroy() {}
#endif

} // namespace fp

#endif /* FP_HOOKS_H */
