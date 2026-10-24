/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_renderer.h The first-person renderer, running inside the OpenGL back-end. */

#ifndef FP_RENDERER_H
#define FP_RENDERER_H

#include "../runtime/fp_view_handoff.h"
#include "../sim/fp_snapshot.h"
#include "fp_gl.h"

namespace fp {

/**
 * Draws the first-person view (docs/fp/architecture.md, section 12).
 *
 * It runs at the end of OpenGLBackend::Paint(), after the 2D interface was
 * drawn, without the game state lock. The scene goes into an offscreen
 * framebuffer, which is then copied onto the parts of the view that no other
 * window covers. So the 2D interface keeps working on top of the 3D view, no
 * matter whether the video buffer is paletted or not.
 *
 * Only data from the view hand-over and the snapshot ring is used; the
 * simulation is never touched.
 */
class Renderer {
public:
	void Paint();
	void Shutdown(bool context_lost);

private:
	/** A chunk of the world mirror on the GPU. */
	struct GpuChunk {
		GLuint vertex_array = 0; ///< Vertex array object.
		GLuint vertex_buffer = 0; ///< Vertex buffer.
		GLuint index_buffer = 0; ///< Index buffer.
		GLsizei index_count = 0; ///< Number of indices.
		Vec3d origin{}; ///< Position of the north corner, in metres.
		float min_z = 0; ///< Lowest vertex, in metres.
		float max_z = 0; ///< Highest vertex, in metres.
	};

	/** Per-instance data of a vehicle box; must match the attributes of BOX_VERTEX_SHADER. */
	struct BoxInstance {
		float x; ///< X coordinate of the centre of the base, relative to the camera.
		float y; ///< Y coordinate of the centre of the base, relative to the camera.
		float z; ///< Z coordinate of the centre of the base, relative to the camera.
		float yaw; ///< Heading, in radians.
		float length; ///< Length, in metres.
		float width; ///< Width, in metres.
		float height; ///< Height, in metres.
		uint8_t r; ///< Red.
		uint8_t g; ///< Green.
		uint8_t b; ///< Blue.
		uint8_t a; ///< Alpha.
	};
	static_assert(sizeof(BoxInstance) == 32);

	/** Uniform locations of the lit programs. */
	struct LitUniforms {
		GLint view_proj = -1; ///< u_view_proj
		GLint offset = -1; ///< u_offset
		GLint sun_dir = -1; ///< u_sun_dir
		GLint sun_colour = -1; ///< u_sun_colour
		GLint ambient = -1; ///< u_ambient
		GLint fog_colour = -1; ///< u_fog_colour
		GLint fog_density = -1; ///< u_fog_density
		GLint grid_offset = -1; ///< u_grid_offset
		GLint tile_length = -1; ///< u_tile_length
		GLint grid_strength = -1; ///< u_grid_strength
	};

	/** Uniform locations of the sky program. */
	struct SkyUniforms {
		GLint forward = -1; ///< u_forward
		GLint right = -1; ///< u_right
		GLint up = -1; ///< u_up
		GLint tan_half_fov = -1; ///< u_tan_half_fov
		GLint zenith = -1; ///< u_zenith
		GLint horizon = -1; ///< u_horizon
		GLint ground = -1; ///< u_ground
		GLint sun_dir = -1; ///< u_sun_dir
	};

	bool initialised = false; ///< Whether all GPU resources exist.
	bool failed = false; ///< Whether initialisation failed; not retried until the next shutdown.
	bool reversed_z = false; ///< Whether reversed-Z depth is used.
	std::string info; ///< Description of the OpenGL context, or why initialisation failed.

	GLuint sky_program = 0; ///< Program of the sky.
	GLuint terrain_program = 0; ///< Program of the terrain.
	GLuint box_program = 0; ///< Program of the vehicle boxes.
	SkyUniforms sky_uniforms{}; ///< Uniforms of #sky_program.
	LitUniforms terrain_uniforms{}; ///< Uniforms of #terrain_program.
	LitUniforms box_uniforms{}; ///< Uniforms of #box_program.

	GLuint empty_vertex_array = 0; ///< Vertex array without attributes, for the sky.
	GLuint box_vertex_array = 0; ///< Vertex array of the boxes.
	GLuint box_vertex_buffer = 0; ///< Corners of the unit box.
	GLuint box_index_buffer = 0; ///< Triangles of the unit box.
	GLuint box_instance_buffer = 0; ///< Per-instance data of the boxes.

	GLuint framebuffer = 0; ///< Offscreen framebuffer of the scene.
	GLuint colour_buffer = 0; ///< Colour attachment of #framebuffer.
	GLuint depth_buffer = 0; ///< Depth attachment of #framebuffer.
	int framebuffer_width = 0; ///< Width of #framebuffer.
	int framebuffer_height = 0; ///< Height of #framebuffer.

	std::unordered_map<ChunkIndex, GpuChunk> chunks; ///< Chunks on the GPU.
	std::unordered_map<ChunkIndex, GpuChunk> overlay_chunks; ///< Meshes of the debug overlay on the GPU, per chunk.
	ViewUpdate update{}; ///< Scratch space for taking updates from the hand-over.
	ViewFrame frame{}; ///< The view being drawn.
	std::vector<BoxInstance> boxes; ///< Scratch list of vehicle boxes.
	std::unordered_map<uint32_t, size_t> previous_vehicles; ///< Index of each vehicle in the previous snapshot.

	bool Init();
	void ApplyChunkOps(std::unordered_map<ChunkIndex, GpuChunk> &target, bool reset, std::vector<ChunkOp> &ops);
	void DeleteChunk(GpuChunk &chunk);
	void EnsureFramebuffer(int width, int height);
	void DrawScene(int width, int height);
	void BuildBoxes(const Vec3d &camera);
	void Composite(const std::array<GLint, 4> &viewport, GLuint target);
};

} // namespace fp

#endif /* FP_RENDERER_H */
