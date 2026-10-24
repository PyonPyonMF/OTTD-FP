/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_rect.h Rectangle arithmetic for compositing the 3D view with the 2D interface. */

#ifndef FP_RECT_H
#define FP_RECT_H

#include "../../core/geometry_type.hpp"

namespace fp {

bool IsEmptyArea(const Rect &r);
Rect IntersectRects(const Rect &a, const Rect &b);
void SubtractRect(const Rect &from, const Rect &hole, std::vector<Rect> &out);
void SubtractRects(std::vector<Rect> &area, const Rect &hole);

} // namespace fp

#endif /* FP_RECT_H */
