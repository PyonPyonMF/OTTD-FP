/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_atlas_texture.cpp Pages of the sprite atlas on the GPU. */

#include "../../stdafx.h"
#include "fp_atlas_texture.h"
#include "../../debug.h"

#include "../../safeguards.h"

namespace fp {

static constexpr float MAX_ANISOTROPY = 8.0f; ///< Anisotropic filtering used where available; sprites seen at grazing angles stay sharp.

/** Create the texture of another page. */
void AtlasTextures::CreatePage()
{
	if (!this->anisotropy_known) {
		this->anisotropy_known = true;
		if (_fpgl.HasExtension("GL_EXT_texture_filter_anisotropic") || _fpgl.HasExtension("GL_ARB_texture_filter_anisotropic")) {
			GLfloat max = 1.0f;
			_fpgl.glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max);
			this->anisotropy = std::min(max, MAX_ANISOTROPY);
		}
	}

	GLuint texture = 0;
	_fpgl.glGenTextures(1, &texture);
	_fpgl.glBindTexture(GL_TEXTURE_2D, texture);
	for (uint level = 0; level < ATLAS_MIP_LEVELS; level++) {
		GLsizei size = ATLAS_PAGE_SIZE >> level;
		_fpgl.glTexImage2D(GL_TEXTURE_2D, level, GL_SRGB8_ALPHA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	}
	_fpgl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	_fpgl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, ATLAS_MIP_LEVELS - 1);
	_fpgl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	_fpgl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	_fpgl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	_fpgl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	if (this->anisotropy > 1.0f) _fpgl.glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, this->anisotropy);

	this->textures.push_back(texture);
	Debug(Facility::Fp, Severity::Debug1, "Sprite atlas page {} created, {}x{} texels", this->textures.size() - 1, ATLAS_PAGE_SIZE, ATLAS_PAGE_SIZE);
}

/**
 * Apply changes of the atlas.
 * @param changes The changes; their uploads are consumed.
 */
void AtlasTextures::Apply(AtlasChanges &changes)
{
	/* Uploads read from client memory, and new pages must not take their contents from a buffer either. */
	_fpgl.glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	_fpgl.glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	_fpgl.glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	_fpgl.glActiveTexture(GL_TEXTURE0);

	/* After a reset the old contents stay on the pages, but nothing refers to them anymore. */
	if (changes.reset) this->pages = 0;
	while (this->textures.size() < changes.pages) this->CreatePage();
	this->pages = changes.pages;

	for (const AtlasUpload &upload : changes.uploads) {
		if (upload.page >= this->pages) continue;

		_fpgl.glBindTexture(GL_TEXTURE_2D, this->textures[upload.page]);
		size_t offset = 0;
		for (uint level = 0; level < ATLAS_MIP_LEVELS; level++) {
			GLsizei width = upload.width >> level;
			GLsizei height = upload.height >> level;
			_fpgl.glTexSubImage2D(GL_TEXTURE_2D, level, upload.x >> level, upload.y >> level, width, height, GL_RGBA, GL_UNSIGNED_BYTE, upload.pixels.data() + offset);
			offset += static_cast<size_t>(width) * height;
		}
	}
	changes.uploads.clear();
}

/** Free all textures; for when the view closes or the OpenGL context goes away. */
void AtlasTextures::Release()
{
	if (_fpgl.glDeleteTextures != nullptr && !this->textures.empty()) {
		_fpgl.glDeleteTextures(static_cast<GLsizei>(this->textures.size()), this->textures.data());
	}
	this->textures.clear();
	this->pages = 0;
	this->anisotropy = 1.0f;
	this->anisotropy_known = false;
}

} // namespace fp
