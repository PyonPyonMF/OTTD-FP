/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_sprite_png.cpp Writing sprite images to PNG files. */

#include "../../stdafx.h"
#include "fp_sprite_png.h"
#include "../../fileio_type.h"

#ifdef WITH_PNG
#	include <png.h>
#endif

#include "../../safeguards.h"

namespace fp {

#ifdef WITH_PNG

/**
 * Error handler of libpng; leaves through the jump buffer.
 * @param png_ptr The write structure.
 * @param message The error.
 */
static void PngError(png_structp png_ptr, png_const_charp message)
{
	*static_cast<std::string *>(png_get_error_ptr(png_ptr)) = message;
	png_longjmp(png_ptr, 1);
}

/**
 * Warning handler of libpng; warnings are of no interest here.
 */
static void PngWarning(png_structp, png_const_charp)
{
}

/**
 * Write an image to a PNG file with alpha channel. The sprite offsets are
 * stored as "oFFs" chunk, so tools can align images as the game does.
 * @param image The image.
 * @param filename Name of the file.
 * @return Why writing failed, or std::nullopt on success.
 */
std::optional<std::string> WriteSpritePng(const SpriteImage &image, const std::string &filename)
{
	if (image.width == 0 || image.height == 0) return "the image is empty";

	auto file = FileHandle::Open(filename, "wb");
	if (!file.has_value()) return fmt::format("cannot open '{}' for writing", filename);

	std::string error;
	png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, &error, PngError, PngWarning);
	if (png_ptr == nullptr) return "out of memory";
	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == nullptr) {
		png_destroy_write_struct(&png_ptr, nullptr);
		return "out of memory";
	}

	/* libpng wants pointers to non-const rows, but it does not write to them. */
	std::vector<png_bytep> rows(image.height);
	for (uint y = 0; y < image.height; y++) {
		rows[y] = const_cast<png_bytep>(reinterpret_cast<const uint8_t *>(&image.pixels[static_cast<size_t>(y) * image.width]));
	}

	if (setjmp(png_jmpbuf(png_ptr))) {
		png_destroy_write_struct(&png_ptr, &info_ptr);
		return fmt::format("libpng: {}", error);
	}

	png_init_io(png_ptr, *file);
	png_set_IHDR(png_ptr, info_ptr, image.width, image.height, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	png_set_oFFs(png_ptr, info_ptr, image.x_offs, image.y_offs, PNG_OFFSET_PIXEL);
	png_write_info(png_ptr, info_ptr);
	png_write_image(png_ptr, rows.data());
	png_write_end(png_ptr, info_ptr);
	png_destroy_write_struct(&png_ptr, &info_ptr);
	return std::nullopt;
}

#else

/**
 * Write an image to a PNG file; this build has no libpng.
 * @return Why writing failed.
 */
std::optional<std::string> WriteSpritePng(const SpriteImage &, const std::string &)
{
	return "this build has no PNG support";
}

#endif /* WITH_PNG */

} // namespace fp
