/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_gl.h OpenGL functions used by the first-person renderer. */

#ifndef FP_GL_H
#define FP_GL_H

#if defined(_WIN32)
#	include <windows.h>
#endif

#define GL_GLEXT_PROTOTYPES
#if defined(__APPLE__)
#	define GL_SILENCE_DEPRECATION
#	include <OpenGL/gl3.h>
#else
#	include <GL/gl.h>
#endif
#include "../../3rdparty/opengl/glext.h"

namespace fp {

/** OpenGL functions the renderer cannot do without; OpenGL 3.3 core provides all of them. */
#define FP_GL_REQUIRED_FUNCTIONS(X) \
	X(glActiveTexture) \
	X(glAttachShader) \
	X(glBindBuffer) \
	X(glBindFramebuffer) \
	X(glBindRenderbuffer) \
	X(glBindTexture) \
	X(glBindVertexArray) \
	X(glBlitFramebuffer) \
	X(glBufferData) \
	X(glCheckFramebufferStatus) \
	X(glClear) \
	X(glClearColor) \
	X(glClearDepth) \
	X(glCompileShader) \
	X(glCreateProgram) \
	X(glCreateShader) \
	X(glCullFace) \
	X(glDeleteBuffers) \
	X(glDeleteFramebuffers) \
	X(glDeleteProgram) \
	X(glDeleteRenderbuffers) \
	X(glDeleteShader) \
	X(glDeleteTextures) \
	X(glDeleteVertexArrays) \
	X(glDepthFunc) \
	X(glDepthMask) \
	X(glDisable) \
	X(glDrawArrays) \
	X(glDrawArraysInstanced) \
	X(glDrawElements) \
	X(glDrawElementsInstanced) \
	X(glEnable) \
	X(glEnableVertexAttribArray) \
	X(glFramebufferRenderbuffer) \
	X(glFrontFace) \
	X(glGenBuffers) \
	X(glGenFramebuffers) \
	X(glGenRenderbuffers) \
	X(glGenTextures) \
	X(glGenVertexArrays) \
	X(glGetBooleanv) \
	X(glGetDoublev) \
	X(glGetError) \
	X(glGetFloatv) \
	X(glGetIntegerv) \
	X(glGetProgramInfoLog) \
	X(glGetProgramiv) \
	X(glGetShaderInfoLog) \
	X(glGetShaderiv) \
	X(glGetString) \
	X(glGetStringi) \
	X(glGetUniformLocation) \
	X(glIsEnabled) \
	X(glLinkProgram) \
	X(glPixelStorei) \
	X(glRenderbufferStorage) \
	X(glScissor) \
	X(glShaderSource) \
	X(glTexImage2D) \
	X(glTexParameterf) \
	X(glTexParameteri) \
	X(glTexSubImage2D) \
	X(glUniform1i) \
	X(glUniform1f) \
	X(glUniform2f) \
	X(glUniform3f) \
	X(glUniformMatrix4fv) \
	X(glUseProgram) \
	X(glVertexAttribDivisor) \
	X(glVertexAttribPointer) \
	X(glViewport)

/** OpenGL functions the renderer uses when they are there. */
#define FP_GL_OPTIONAL_FUNCTIONS(X) \
	X(glClipControl)

/** Table of the OpenGL functions of the renderer, loaded through the same loader as the OpenGL back-end. */
struct GLApi {
#define FP_GL_DECLARE(name) decltype(&::name) name = nullptr;
	FP_GL_REQUIRED_FUNCTIONS(FP_GL_DECLARE)
	FP_GL_OPTIONAL_FUNCTIONS(FP_GL_DECLARE)
#undef FP_GL_DECLARE

	bool has_clip_control = false; ///< Whether glClipControl() may be used, which enables reversed-Z depth.

	std::optional<std::string> Load();
	bool HasExtension(std::string_view name) const;
};

extern GLApi _fpgl;

} // namespace fp

#endif /* FP_GL_H */
