/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_gl.cpp OpenGL functions used by the first-person renderer. */

#include "../../stdafx.h"
#include "fp_gl.h"
#include "../../video/opengl.h"

#include "../../safeguards.h"

extern GetOGLProcAddressProc GetOGLProcAddress;

namespace fp {

GLApi _fpgl; ///< The OpenGL functions of the renderer.

/**
 * Load all functions. Must be called with the OpenGL context of the back-end current.
 * @return std::nullopt on success, otherwise what is missing.
 */
std::optional<std::string> GLApi::Load()
{
	*this = {};

	if (GetOGLProcAddress == nullptr) return "no OpenGL loader";
	if (!IsOpenGLVersionAtLeast(3, 3)) return "OpenGL 3.3 or newer is required";

#define FP_GL_LOAD_REQUIRED(name) \
	this->name = reinterpret_cast<decltype(this->name)>(GetOGLProcAddress(#name)); \
	if (this->name == nullptr) return "missing " #name;
	FP_GL_REQUIRED_FUNCTIONS(FP_GL_LOAD_REQUIRED)
#undef FP_GL_LOAD_REQUIRED

#define FP_GL_LOAD_OPTIONAL(name) this->name = reinterpret_cast<decltype(this->name)>(GetOGLProcAddress(#name));
	FP_GL_OPTIONAL_FUNCTIONS(FP_GL_LOAD_OPTIONAL)
#undef FP_GL_LOAD_OPTIONAL

	/* Loaders may return entry points that the context does not support, so check the version as well. */
	this->has_clip_control = this->glClipControl != nullptr && (IsOpenGLVersionAtLeast(4, 5) || this->HasExtension("GL_ARB_clip_control"));
	return std::nullopt;
}

/**
 * Check whether the current context supports an extension.
 * @param name Name of the extension.
 * @return True iff it is supported.
 */
bool GLApi::HasExtension(std::string_view name) const
{
	GLint count = 0;
	this->glGetIntegerv(GL_NUM_EXTENSIONS, &count);
	for (GLint i = 0; i < count; i++) {
		const char *extension = reinterpret_cast<const char *>(this->glGetStringi(GL_EXTENSIONS, i));
		if (extension != nullptr && name == extension) return true;
	}
	return false;
}

} // namespace fp
