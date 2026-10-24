/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_widget.h Types related to the first-person window widgets. */

#ifndef FP_WIDGET_H
#define FP_WIDGET_H

#include "../../window_type.h"

/** Widgets of the first-person window. */
enum FirstPersonViewWidgets : WidgetID {
	WID_FPV_CAPTION, ///< Caption of the window.
	WID_FPV_VIEW, ///< The 3D view.
	WID_FPV_STATUS, ///< Status bar.
};

#endif /* FP_WIDGET_H */
