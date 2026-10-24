/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_sprite_decode.h Turning sprites of the sprite loader into images. */

#ifndef FP_SPRITE_DECODE_H
#define FP_SPRITE_DECODE_H

#include "../../gfx_type.h"
#include "fp_sprite_image.h"

namespace fp {

ZoomLevel SelectDetailedZoom(const SpriteLoader::SpriteCollection &collection);
void CaptureSprite(const SpriteLoader::SpriteCollection &collection, RawSprite &raw);
void ApplyPalette(const RawSprite &raw, std::span<const Colour, 256> palette, const SpriteRemap &remap, SpriteImage &image);
void ComposeImages(std::span<const SpriteImage *const> layers, SpriteImage &image);

} // namespace fp

#endif /* FP_SPRITE_DECODE_H */
