/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_atlas_texture.h Pages of the sprite atlas on the GPU. */

#ifndef FP_ATLAS_TEXTURE_H
#define FP_ATLAS_TEXTURE_H

#include "../sprites/fp_sprite_atlas.h"
#include "fp_gl.h"

namespace fp {

/**
 * The pages of the sprite atlas as OpenGL textures.
 *
 * Each page is a texture of its own, so adding a page never moves the
 * others. Pages are sRGB, which makes filtering and mipmapping happen in
 * linear light; the mipmap levels come ready made with the uploads.
 *
 * All members must be used on the drawing thread with the OpenGL context current.
 */
class AtlasTextures {
public:
	void Apply(AtlasChanges &changes);
	void Release();

	/**
	 * Texture of a page.
	 * @param page The page.
	 * @return The texture, or 0 if the page is not in use.
	 */
	GLuint GetTexture(uint page) const { return page < this->pages ? this->textures[page] : 0; }

	/**
	 * Pages in use.
	 * @return Number of pages.
	 */
	uint GetPageCount() const { return this->pages; }

private:
	std::vector<GLuint> textures; ///< All page textures; those beyond #pages are kept for reuse.
	uint pages = 0; ///< Pages in use.
	float anisotropy = 1.0f; ///< Anisotropic filtering of the pages; 1 is none.
	bool anisotropy_known = false; ///< Whether #anisotropy was determined for this context.

	void CreatePage();
};

} // namespace fp

#endif /* FP_ATLAS_TEXTURE_H */
