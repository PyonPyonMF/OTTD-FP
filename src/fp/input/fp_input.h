/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_input.h Abstract input actions of the first-person mode. */

#ifndef FP_INPUT_H
#define FP_INPUT_H

namespace fp {

/**
 * Values of the input actions for one frame (docs/fp/architecture.md, section 14).
 *
 * Roles only see actions, never keys or devices, so keyboard, mouse,
 * gamepads and HOTAS can be bound to the same actions later on.
 */
struct InputState {
	float move_forward = 0; ///< Move forward (positive) or backward (negative), -1 to 1.
	float move_right = 0; ///< Move right (positive) or left (negative), -1 to 1.
	float move_up = 0; ///< Impulses to move up (positive) or down (negative), e.g. mouse wheel notches.
	float look_yaw = 0; ///< Turn this frame, in radians; positive turns left.
	float look_pitch = 0; ///< Pitch this frame, in radians; positive looks up.
	bool boost = false; ///< Move faster.
};

} // namespace fp

#endif /* FP_INPUT_H */
