/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file first_person_gui.h The first-person view: walking around in the 3D view, and riding along inside vehicles. */

#ifndef FIRST_PERSON_GUI_H
#define FIRST_PERSON_GUI_H

#include "vehicle_type.h"
#include "window_type.h"

void PlaceWindowLike(Window *w, const Window *like);
bool ShowFirstPersonBeside(const Vehicle *v, const Window *replace);
bool FirstPersonCapturesDirectionKeys();
void FirstPersonVehicleGone(const Vehicle *v);
void RegisterFirstPersonConsoleCommands();

#endif /* FIRST_PERSON_GUI_H */
