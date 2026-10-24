/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_sprite_png.h Writing sprite images to PNG files. */

#ifndef FP_SPRITE_PNG_H
#define FP_SPRITE_PNG_H

#include "fp_sprite_image.h"

namespace fp {

std::optional<std::string> WriteSpritePng(const SpriteImage &image, const std::string &filename);

} // namespace fp

#endif /* FP_SPRITE_PNG_H */
