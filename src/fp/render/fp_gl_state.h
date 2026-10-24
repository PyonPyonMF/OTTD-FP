/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_gl_state.h Saving and restoring the OpenGL state around the first-person renderer. */

#ifndef FP_GL_STATE_H
#define FP_GL_STATE_H

#include "fp_gl.h"

namespace fp {

/**
 * Saves the OpenGL state the renderer touches and restores it on destruction.
 * The renderer runs in the middle of the frame of the OpenGL back-end, which
 * expects to find the state as it left it.
 */
class GLStateGuard {
public:
	GLStateGuard()
	{
		_fpgl.glGetIntegerv(GL_CURRENT_PROGRAM, &this->program);
		_fpgl.glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &this->vertex_array);
		_fpgl.glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &this->array_buffer);
		_fpgl.glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &this->draw_framebuffer);
		_fpgl.glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &this->read_framebuffer);
		_fpgl.glGetIntegerv(GL_RENDERBUFFER_BINDING, &this->renderbuffer);
		_fpgl.glGetIntegerv(GL_ACTIVE_TEXTURE, &this->active_texture);
		_fpgl.glGetIntegerv(GL_VIEWPORT, this->viewport.data());
		_fpgl.glGetIntegerv(GL_SCISSOR_BOX, this->scissor.data());
		_fpgl.glGetIntegerv(GL_DEPTH_FUNC, &this->depth_func);
		_fpgl.glGetIntegerv(GL_CULL_FACE_MODE, &this->cull_face_mode);
		_fpgl.glGetIntegerv(GL_FRONT_FACE, &this->front_face);
		_fpgl.glGetBooleanv(GL_DEPTH_WRITEMASK, &this->depth_mask);
		_fpgl.glGetDoublev(GL_DEPTH_CLEAR_VALUE, &this->clear_depth);
		_fpgl.glGetFloatv(GL_COLOR_CLEAR_VALUE, this->clear_colour.data());
		if (_fpgl.has_clip_control) {
			_fpgl.glGetIntegerv(GL_CLIP_ORIGIN, &this->clip_origin);
			_fpgl.glGetIntegerv(GL_CLIP_DEPTH_MODE, &this->clip_depth_mode);
		}
		for (size_t i = 0; i < CAPABILITIES.size(); i++) this->enabled[i] = _fpgl.glIsEnabled(CAPABILITIES[i]);
	}

	~GLStateGuard()
	{
		for (size_t i = 0; i < CAPABILITIES.size(); i++) {
			if (this->enabled[i]) {
				_fpgl.glEnable(CAPABILITIES[i]);
			} else {
				_fpgl.glDisable(CAPABILITIES[i]);
			}
		}
		if (_fpgl.has_clip_control) _fpgl.glClipControl(this->clip_origin, this->clip_depth_mode);
		_fpgl.glClearColor(this->clear_colour[0], this->clear_colour[1], this->clear_colour[2], this->clear_colour[3]);
		_fpgl.glClearDepth(this->clear_depth);
		_fpgl.glDepthMask(this->depth_mask);
		_fpgl.glFrontFace(this->front_face);
		_fpgl.glCullFace(this->cull_face_mode);
		_fpgl.glDepthFunc(this->depth_func);
		_fpgl.glScissor(this->scissor[0], this->scissor[1], this->scissor[2], this->scissor[3]);
		_fpgl.glViewport(this->viewport[0], this->viewport[1], this->viewport[2], this->viewport[3]);
		_fpgl.glActiveTexture(this->active_texture);
		_fpgl.glBindRenderbuffer(GL_RENDERBUFFER, this->renderbuffer);
		_fpgl.glBindFramebuffer(GL_READ_FRAMEBUFFER, this->read_framebuffer);
		_fpgl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, this->draw_framebuffer);
		_fpgl.glBindBuffer(GL_ARRAY_BUFFER, this->array_buffer);
		_fpgl.glBindVertexArray(this->vertex_array);
		_fpgl.glUseProgram(this->program);
	}

	GLStateGuard(const GLStateGuard &) = delete;
	GLStateGuard &operator=(const GLStateGuard &) = delete;

	/**
	 * Viewport at the time the guard was created; the OpenGL back-end keeps it at the size of the screen.
	 * @return x, y, width and height.
	 */
	const std::array<GLint, 4> &GetViewport() const { return this->viewport; }

	/**
	 * Draw framebuffer at the time the guard was created.
	 * @return The framebuffer object; 0 for the window.
	 */
	GLuint GetDrawFramebuffer() const { return static_cast<GLuint>(this->draw_framebuffer); }

private:
	/** Capabilities the renderer may change. */
	static constexpr std::array<GLenum, 4> CAPABILITIES = {GL_BLEND, GL_CULL_FACE, GL_DEPTH_TEST, GL_SCISSOR_TEST};

	GLint program = 0; ///< Current program.
	GLint vertex_array = 0; ///< Bound vertex array object.
	GLint array_buffer = 0; ///< Bound array buffer.
	GLint draw_framebuffer = 0; ///< Bound draw framebuffer.
	GLint read_framebuffer = 0; ///< Bound read framebuffer.
	GLint renderbuffer = 0; ///< Bound renderbuffer.
	GLint active_texture = GL_TEXTURE0; ///< Active texture unit.
	std::array<GLint, 4> viewport{}; ///< Viewport.
	std::array<GLint, 4> scissor{}; ///< Scissor box.
	GLint depth_func = GL_LESS; ///< Depth function.
	GLint cull_face_mode = GL_BACK; ///< Faces that are culled.
	GLint front_face = GL_CCW; ///< Winding of front faces.
	GLboolean depth_mask = GL_TRUE; ///< Whether depth is written.
	GLdouble clear_depth = 1.0; ///< Depth clear value.
	std::array<GLfloat, 4> clear_colour{}; ///< Colour clear value.
	GLint clip_origin = GL_LOWER_LEFT; ///< Clip control origin.
	GLint clip_depth_mode = GL_NEGATIVE_ONE_TO_ONE; ///< Clip control depth mode.
	std::array<GLboolean, CAPABILITIES.size()> enabled{}; ///< Whether each of #CAPABILITIES was enabled.
};

} // namespace fp

#endif /* FP_GL_STATE_H */
