/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file first_person_widget.h Types related to the widgets of the first-person view. */

#ifndef WIDGETS_FIRST_PERSON_WIDGET_H
#define WIDGETS_FIRST_PERSON_WIDGET_H

/** Widgets of the #FirstPersonWindow class. */
enum FirstPersonWidgets : WidgetID {
	WID_FP_CAPTION, ///< Caption of window.
	WID_FP_VIEW, ///< The 3D view.
	WID_FP_STATUS, ///< Where one is and what one can do.
	WID_FP_GET_IN, ///< Get into the vehicle next to one, or get off the vehicle one is in.
	WID_FP_SIT, ///< Sit down on a seat, or stand up.
};

#endif /* WIDGETS_FIRST_PERSON_WIDGET_H */
