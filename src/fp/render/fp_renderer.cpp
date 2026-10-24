/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_renderer.cpp The first-person renderer, running inside the OpenGL back-end. */

#include "../../stdafx.h"
#include "fp_renderer.h"
#include "fp_gl_state.h"
#include "fp_shaders.h"
#include "../fp_hooks.h"
#include "../runtime/fp_runtime.h"
#include "../sim/fp_bridge.h"
#include "../../debug.h"
#include "../../direction_type.h"

#include "../../safeguards.h"

namespace fp {

static Renderer _renderer; ///< The renderer.

/* Light and sky of milestone M0, colours in sRGB. */
static constexpr Vec3f SKY_ZENITH{0.27f, 0.47f, 0.75f}; ///< Colour of the sky straight up.
static constexpr Vec3f SKY_HORIZON{0.70f, 0.79f, 0.88f}; ///< Colour of the sky at the horizon, also used for fog.
static constexpr Vec3f SKY_GROUND{0.46f, 0.50f, 0.46f}; ///< Colour below the horizon.
static constexpr Vec3f SUN_COLOUR{1.00f, 0.95f, 0.86f}; ///< Colour of sunlight, linear.
static constexpr Vec3f AMBIENT{0.28f, 0.32f, 0.38f}; ///< Colour of ambient light, linear.
static constexpr float Z_NEAR = 0.3f; ///< Distance of the near plane, in metres.

/** Corners of the unit box: x and y from -0.5 to 0.5, z from 0 to 1. */
static constexpr std::array<float, 24> BOX_CORNERS = {
	-0.5f, -0.5f, 0.0f,   0.5f, -0.5f, 0.0f,   0.5f, 0.5f, 0.0f,   -0.5f, 0.5f, 0.0f,
	-0.5f, -0.5f, 1.0f,   0.5f, -0.5f, 1.0f,   0.5f, 0.5f, 1.0f,   -0.5f, 0.5f, 1.0f,
};

/** Triangles of the unit box. */
static constexpr std::array<uint16_t, 36> BOX_INDICES = {
	0, 2, 1,  0, 3, 2, // Bottom.
	4, 5, 6,  4, 6, 7, // Top.
	0, 1, 5,  0, 5, 4, // -Y side.
	1, 2, 6,  1, 6, 5, // +X side.
	2, 3, 7,  2, 7, 6, // +Y side.
	3, 0, 4,  3, 4, 7, // -X side.
};

/** Movement per step in each direction, see GetNewVehiclePos(). */
static constexpr std::array<std::pair<int, int>, 8> DIRECTION_DELTAS = {{
	{-1, -1}, {-1, 0}, {-1, 1}, {0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1},
}};

/**
 * Direction of the sun.
 * @return Unit vector towards the sun.
 */
static Vec3f SunDirection()
{
	static const Vec3f direction = Normalise(Vec3f{0.45f, 0.30f, 0.84f});
	return direction;
}

/**
 * Heading of a direction.
 * @param direction The direction.
 * @return The heading in radians, counter-clockwise from the X axis.
 */
static double DirectionYaw(Direction direction)
{
	if (direction >= Direction::End) return 0.0;
	auto [dx, dy] = DIRECTION_DELTAS[to_underlying(direction)];
	return std::atan2(dy, dx);
}

/**
 * Position of a vehicle part, including its progress towards the next step.
 * The simulation moves vehicles in steps of 1/16 tile; the progress tells how
 * far the vehicle is between two steps, which makes the motion continuous.
 * @param frame The vehicle part.
 * @return The position, in world units.
 */
static Vec3d FramePosition(const VehicleFrame &frame)
{
	Vec3d position{static_cast<double>(frame.x), static_cast<double>(frame.y), static_cast<double>(frame.z)};
	if (frame.type == VehicleType::Aircraft || frame.advance == 0 || frame.moving_direction >= Direction::End) return position;

	double t = static_cast<double>(frame.progress) / frame.advance;
	auto [dx, dy] = DIRECTION_DELTAS[to_underlying(frame.moving_direction)];
	position.x += dx * t;
	position.y += dy * t;
	return position;
}

/**
 * Compile a shader.
 * @param type Type of shader.
 * @param source GLSL source.
 * @param[out] error Receives the reason of failure.
 * @return The shader, or 0 on failure.
 */
static GLuint CompileShader(GLenum type, std::string_view source, std::string &error)
{
	GLuint shader = _fpgl.glCreateShader(type);
	const GLchar *text = source.data();
	GLint length = static_cast<GLint>(source.size());
	_fpgl.glShaderSource(shader, 1, &text, &length);
	_fpgl.glCompileShader(shader);

	GLint status = GL_FALSE;
	_fpgl.glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (status == GL_TRUE) return shader;

	GLint log_length = 0;
	_fpgl.glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
	std::string log(std::max(log_length, 1), '\0');
	_fpgl.glGetShaderInfoLog(shader, log_length, nullptr, log.data());
	error = fmt::format("shader compilation failed: {}", log.c_str());
	_fpgl.glDeleteShader(shader);
	return 0;
}

/**
 * Compile and link a program.
 * @param vertex GLSL source of the vertex shader.
 * @param fragment GLSL source of the fragment shader.
 * @param[out] error Receives the reason of failure.
 * @return The program, or 0 on failure.
 */
static GLuint LinkProgram(std::string_view vertex, std::string_view fragment, std::string &error)
{
	GLuint vertex_shader = CompileShader(GL_VERTEX_SHADER, vertex, error);
	if (vertex_shader == 0) return 0;
	GLuint fragment_shader = CompileShader(GL_FRAGMENT_SHADER, fragment, error);
	if (fragment_shader == 0) {
		_fpgl.glDeleteShader(vertex_shader);
		return 0;
	}

	GLuint program = _fpgl.glCreateProgram();
	_fpgl.glAttachShader(program, vertex_shader);
	_fpgl.glAttachShader(program, fragment_shader);
	_fpgl.glLinkProgram(program);
	/* The shaders are flagged for deletion and go away with the program. */
	_fpgl.glDeleteShader(vertex_shader);
	_fpgl.glDeleteShader(fragment_shader);

	GLint status = GL_FALSE;
	_fpgl.glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (status == GL_TRUE) return program;

	GLint log_length = 0;
	_fpgl.glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
	std::string log(std::max(log_length, 1), '\0');
	_fpgl.glGetProgramInfoLog(program, log_length, nullptr, log.data());
	error = fmt::format("program linking failed: {}", log.c_str());
	_fpgl.glDeleteProgram(program);
	return 0;
}

/**
 * Create all GPU resources that do not depend on the view.
 * @return True on success; otherwise #info tells why.
 */
bool Renderer::Init()
{
	if (auto error = _fpgl.Load(); error.has_value()) {
		this->info = *error;
		return false;
	}

	std::string error;
	this->sky_program = LinkProgram(SKY_VERTEX_SHADER, SKY_FRAGMENT_SHADER, error);
	if (this->sky_program != 0) this->terrain_program = LinkProgram(TERRAIN_VERTEX_SHADER, LIT_FRAGMENT_SHADER, error);
	if (this->terrain_program != 0) this->box_program = LinkProgram(BOX_VERTEX_SHADER, LIT_FRAGMENT_SHADER, error);
	if (this->box_program == 0) {
		this->info = error;
		this->Shutdown(false);
		return false;
	}

	this->sky_uniforms.forward = _fpgl.glGetUniformLocation(this->sky_program, "u_forward");
	this->sky_uniforms.right = _fpgl.glGetUniformLocation(this->sky_program, "u_right");
	this->sky_uniforms.up = _fpgl.glGetUniformLocation(this->sky_program, "u_up");
	this->sky_uniforms.tan_half_fov = _fpgl.glGetUniformLocation(this->sky_program, "u_tan_half_fov");
	this->sky_uniforms.zenith = _fpgl.glGetUniformLocation(this->sky_program, "u_zenith");
	this->sky_uniforms.horizon = _fpgl.glGetUniformLocation(this->sky_program, "u_horizon");
	this->sky_uniforms.ground = _fpgl.glGetUniformLocation(this->sky_program, "u_ground");
	this->sky_uniforms.sun_dir = _fpgl.glGetUniformLocation(this->sky_program, "u_sun_dir");

	auto lit_uniforms = [](GLuint program) {
		LitUniforms u;
		u.view_proj = _fpgl.glGetUniformLocation(program, "u_view_proj");
		u.offset = _fpgl.glGetUniformLocation(program, "u_offset");
		u.sun_dir = _fpgl.glGetUniformLocation(program, "u_sun_dir");
		u.sun_colour = _fpgl.glGetUniformLocation(program, "u_sun_colour");
		u.ambient = _fpgl.glGetUniformLocation(program, "u_ambient");
		u.fog_colour = _fpgl.glGetUniformLocation(program, "u_fog_colour");
		u.fog_density = _fpgl.glGetUniformLocation(program, "u_fog_density");
		u.grid_offset = _fpgl.glGetUniformLocation(program, "u_grid_offset");
		u.tile_length = _fpgl.glGetUniformLocation(program, "u_tile_length");
		u.grid_strength = _fpgl.glGetUniformLocation(program, "u_grid_strength");
		return u;
	};
	this->terrain_uniforms = lit_uniforms(this->terrain_program);
	this->box_uniforms = lit_uniforms(this->box_program);

	/* The sky needs a vertex array object to draw, but no attributes. */
	_fpgl.glGenVertexArrays(1, &this->empty_vertex_array);

	/* The unit box, with per-instance placement. */
	_fpgl.glGenVertexArrays(1, &this->box_vertex_array);
	_fpgl.glBindVertexArray(this->box_vertex_array);

	_fpgl.glGenBuffers(1, &this->box_vertex_buffer);
	_fpgl.glBindBuffer(GL_ARRAY_BUFFER, this->box_vertex_buffer);
	_fpgl.glBufferData(GL_ARRAY_BUFFER, sizeof(BOX_CORNERS), BOX_CORNERS.data(), GL_STATIC_DRAW);
	_fpgl.glEnableVertexAttribArray(0);
	_fpgl.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);

	_fpgl.glGenBuffers(1, &this->box_index_buffer);
	_fpgl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->box_index_buffer);
	_fpgl.glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(BOX_INDICES), BOX_INDICES.data(), GL_STATIC_DRAW);

	_fpgl.glGenBuffers(1, &this->box_instance_buffer);
	_fpgl.glBindBuffer(GL_ARRAY_BUFFER, this->box_instance_buffer);
	_fpgl.glEnableVertexAttribArray(1);
	_fpgl.glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(BoxInstance), reinterpret_cast<void *>(offsetof(BoxInstance, x)));
	_fpgl.glVertexAttribDivisor(1, 1);
	_fpgl.glEnableVertexAttribArray(2);
	_fpgl.glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(BoxInstance), reinterpret_cast<void *>(offsetof(BoxInstance, length)));
	_fpgl.glVertexAttribDivisor(2, 1);
	_fpgl.glEnableVertexAttribArray(3);
	_fpgl.glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(BoxInstance), reinterpret_cast<void *>(offsetof(BoxInstance, r)));
	_fpgl.glVertexAttribDivisor(3, 1);

	this->reversed_z = _fpgl.has_clip_control;

	const char *renderer = reinterpret_cast<const char *>(_fpgl.glGetString(GL_RENDERER));
	const char *version = reinterpret_cast<const char *>(_fpgl.glGetString(GL_VERSION));
	this->info = fmt::format("{}, OpenGL {}{}", renderer != nullptr ? renderer : "?", version != nullptr ? version : "?", this->reversed_z ? ", reversed-Z" : "");
	this->initialised = true;

	Debug(Facility::Fp, Severity::Info, "First-person renderer started: {}", this->info);
	return true;
}

/**
 * Release all GPU resources.
 * @param context_lost Whether this happens because the OpenGL context goes away;
 *                     the runtime then has to send all chunks again.
 */
void Renderer::Shutdown(bool context_lost)
{
	bool had_chunks = !this->chunks.empty();

	if (_fpgl.glDeleteProgram != nullptr) {
		for (auto &[index, chunk] : this->chunks) this->DeleteChunk(chunk);

		_fpgl.glDeleteProgram(this->sky_program);
		_fpgl.glDeleteProgram(this->terrain_program);
		_fpgl.glDeleteProgram(this->box_program);
		_fpgl.glDeleteVertexArrays(1, &this->empty_vertex_array);
		_fpgl.glDeleteVertexArrays(1, &this->box_vertex_array);
		_fpgl.glDeleteBuffers(1, &this->box_vertex_buffer);
		_fpgl.glDeleteBuffers(1, &this->box_index_buffer);
		_fpgl.glDeleteBuffers(1, &this->box_instance_buffer);
		_fpgl.glDeleteFramebuffers(1, &this->framebuffer);
		_fpgl.glDeleteRenderbuffers(1, &this->colour_buffer);
		_fpgl.glDeleteRenderbuffers(1, &this->depth_buffer);
	}

	this->chunks.clear();
	this->sky_program = this->terrain_program = this->box_program = 0;
	this->empty_vertex_array = this->box_vertex_array = 0;
	this->box_vertex_buffer = this->box_index_buffer = this->box_instance_buffer = 0;
	this->framebuffer = this->colour_buffer = this->depth_buffer = 0;
	this->framebuffer_width = this->framebuffer_height = 0;
	this->previous_vehicles.clear();
	this->initialised = false;
	this->failed = false;

	if (context_lost && had_chunks) Runtime::Get().GetHandoff().ReportContextLost();
}

/**
 * Free the GPU resources of a chunk.
 * @param chunk The chunk.
 */
void Renderer::DeleteChunk(GpuChunk &chunk)
{
	_fpgl.glDeleteVertexArrays(1, &chunk.vertex_array);
	_fpgl.glDeleteBuffers(1, &chunk.vertex_buffer);
	_fpgl.glDeleteBuffers(1, &chunk.index_buffer);
	chunk = {};
}

/**
 * Apply chunk changes from the world mirror.
 * @param reset Whether to drop all chunks first.
 * @param ops The changes, in order; emptied afterwards.
 */
void Renderer::ApplyChunkOps(bool reset, std::vector<ChunkOp> &ops)
{
	if (reset) {
		for (auto &[index, chunk] : this->chunks) this->DeleteChunk(chunk);
		this->chunks.clear();
	}

	for (ChunkOp &op : ops) {
		if (op.remove) {
			if (auto it = this->chunks.find(op.index); it != this->chunks.end()) {
				this->DeleteChunk(it->second);
				this->chunks.erase(it);
			}
			continue;
		}
		/* Only the terrain is drawn yet; changes of the sprites alone do not concern it. */
		if (!op.mesh.has_value()) continue;

		GpuChunk &chunk = this->chunks[op.index];
		if (chunk.vertex_array == 0) {
			_fpgl.glGenVertexArrays(1, &chunk.vertex_array);
			_fpgl.glGenBuffers(1, &chunk.vertex_buffer);
			_fpgl.glGenBuffers(1, &chunk.index_buffer);

			_fpgl.glBindVertexArray(chunk.vertex_array);
			_fpgl.glBindBuffer(GL_ARRAY_BUFFER, chunk.vertex_buffer);
			_fpgl.glEnableVertexAttribArray(0);
			_fpgl.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), reinterpret_cast<void *>(offsetof(TerrainVertex, x)));
			_fpgl.glEnableVertexAttribArray(1);
			_fpgl.glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(TerrainVertex), reinterpret_cast<void *>(offsetof(TerrainVertex, r)));
		}

		const TerrainMesh &mesh = *op.mesh;
		/* Bind the vertex array first: the element buffer binding is part of its state. */
		_fpgl.glBindVertexArray(chunk.vertex_array);
		_fpgl.glBindBuffer(GL_ARRAY_BUFFER, chunk.vertex_buffer);
		_fpgl.glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(TerrainVertex), mesh.vertices.data(), GL_STATIC_DRAW);
		_fpgl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunk.index_buffer);
		_fpgl.glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(uint16_t), mesh.indices.data(), GL_STATIC_DRAW);

		chunk.index_count = static_cast<GLsizei>(mesh.indices.size());
		chunk.origin = op.origin;
		chunk.min_z = 0;
		chunk.max_z = 0;
		if (!mesh.vertices.empty()) {
			auto [lowest, highest] = std::minmax_element(mesh.vertices.begin(), mesh.vertices.end(), [](const TerrainVertex &a, const TerrainVertex &b) { return a.z < b.z; });
			chunk.min_z = lowest->z;
			chunk.max_z = highest->z;
		}
	}
	ops.clear();
	_fpgl.glBindVertexArray(0);
}

/**
 * Make sure the offscreen framebuffer has the size of the view.
 * @param width Width of the view.
 * @param height Height of the view.
 */
void Renderer::EnsureFramebuffer(int width, int height)
{
	if (this->framebuffer != 0 && width == this->framebuffer_width && height == this->framebuffer_height) return;

	if (this->framebuffer == 0) {
		_fpgl.glGenFramebuffers(1, &this->framebuffer);
		_fpgl.glGenRenderbuffers(1, &this->colour_buffer);
		_fpgl.glGenRenderbuffers(1, &this->depth_buffer);
	}

	_fpgl.glBindRenderbuffer(GL_RENDERBUFFER, this->colour_buffer);
	_fpgl.glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
	_fpgl.glBindRenderbuffer(GL_RENDERBUFFER, this->depth_buffer);
	_fpgl.glRenderbufferStorage(GL_RENDERBUFFER, this->reversed_z ? GL_DEPTH_COMPONENT32F : GL_DEPTH_COMPONENT24, width, height);

	_fpgl.glBindFramebuffer(GL_FRAMEBUFFER, this->framebuffer);
	_fpgl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, this->colour_buffer);
	_fpgl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, this->depth_buffer);

	GLenum status = _fpgl.glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE) Debug(Facility::Fp, Severity::Error, "Offscreen framebuffer incomplete: 0x{:x}", status);

	this->framebuffer_width = width;
	this->framebuffer_height = height;
}

/**
 * Collect the vehicle boxes of this frame, interpolated between the last two snapshots.
 * The view runs one tick behind the simulation, so there is always a pair to interpolate between.
 * @param camera Position of the camera, in metres.
 */
void Renderer::BuildBoxes(const Vec3d &camera)
{
	this->boxes.clear();

	SnapshotRing<TickSnapshot> &ring = Bridge::Get().GetSnapshots();
	if (ring.Acquire()) {
		this->previous_vehicles.clear();
		const std::vector<VehicleFrame> &previous = ring.Previous().vehicles;
		for (size_t i = 0; i < previous.size(); i++) this->previous_vehicles[previous[i].id.base()] = i;
	}

	const TickSnapshot &current = ring.Current();
	const TickSnapshot &previous = ring.Previous();
	if (current.session != this->frame.session) return;

	bool interpolate = previous.session == current.session && previous.tick < current.tick;
	double alpha = 1.0;
	if (interpolate) {
		double interval = std::chrono::duration<double>(current.time - previous.time).count();
		double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - current.time).count();
		alpha = interval > 0 ? Clamp(elapsed / interval, 0.0, 1.0) : 1.0;
	}

	const MetricScale &scale = this->frame.scale;
	for (const VehicleFrame &vehicle : current.vehicles) {
		if (vehicle.flags.Test(VehicleFrameFlag::Hidden)) continue;

		Vec3d position = FramePosition(vehicle);
		double yaw = DirectionYaw(vehicle.direction);
		if (interpolate) {
			auto it = this->previous_vehicles.find(vehicle.id.base());
			if (it != this->previous_vehicles.end() && previous.vehicles[it->second].generation == vehicle.generation) {
				const VehicleFrame &before = previous.vehicles[it->second];
				position = Lerp(FramePosition(before), position, alpha);
				yaw = LerpAngle(DirectionYaw(before.direction), yaw, alpha);
			}
		}

		Vec3d relative = scale.WorldToMetres(position.x, position.y, position.z) - camera;
		if (std::hypot(relative.x, relative.y) > this->frame.view_distance) continue;

		BoxInstance &box = this->boxes.emplace_back();
		box.x = static_cast<float>(relative.x);
		box.y = static_cast<float>(relative.y);
		box.z = static_cast<float>(relative.z);
		box.yaw = static_cast<float>(yaw);
		box.a = 255;

		/* Ground vehicles take their real length; a full length part is half a tile. */
		float length = static_cast<float>(std::max<int>(vehicle.length, 1) * scale.metres_per_unit) - 0.6f;
		switch (vehicle.type) {
			case VehicleType::Train:
				box.length = length; box.width = 3.0f; box.height = 4.0f;
				if (vehicle.flags.Test(VehicleFrameFlag::Front)) {
					box.r = 196; box.g = 64; box.b = 44;
				} else {
					box.r = 150; box.g = 154; box.b = 164;
				}
				break;

			case VehicleType::Road:
				box.length = length; box.width = 2.5f; box.height = 3.0f;
				box.r = 232; box.g = 196; box.b = 52;
				break;

			case VehicleType::Ship:
				box.length = 30.0f; box.width = 8.0f; box.height = 6.0f;
				box.r = 236; box.g = 236; box.b = 236;
				break;

			default:
				if (vehicle.flags.Test(VehicleFrameFlag::Helicopter)) {
					box.length = 14.0f; box.width = 12.0f; box.height = 4.0f;
				} else {
					box.length = 32.0f; box.width = 30.0f; box.height = 6.0f;
				}
				box.r = 240; box.g = 240; box.b = 248;
				break;
		}
		if (vehicle.flags.Test(VehicleFrameFlag::Crashed)) {
			box.r = 60; box.g = 60; box.b = 60;
		}
	}
}

/**
 * Draw the scene into the offscreen framebuffer.
 * @param width Width of the view.
 * @param height Height of the view.
 */
void Renderer::DrawScene(int width, int height)
{
	const CameraPose &camera = this->frame.camera;
	const MetricScale &scale = this->frame.scale;
	const float aspect = static_cast<float>(width) / height;
	const float view_distance = static_cast<float>(this->frame.view_distance);

	Vec3f forward = Vec3f(camera.Forward());
	Vec3f right = Normalise(Cross(forward, Vec3f{0, 0, 1}));
	Vec3f up = Cross(right, forward);
	Mat4f view_proj = MakePerspective(static_cast<float>(camera.fov_y), aspect, Z_NEAR, view_distance * 1.5f, this->reversed_z) * MakeViewRotation(forward);

	_fpgl.glBindFramebuffer(GL_FRAMEBUFFER, this->framebuffer);
	_fpgl.glViewport(0, 0, width, height);
	_fpgl.glDisable(GL_BLEND);
	_fpgl.glDisable(GL_SCISSOR_TEST);

	if (this->reversed_z) {
		_fpgl.glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
		_fpgl.glClearDepth(0.0);
		_fpgl.glDepthFunc(GL_GREATER);
	} else {
		_fpgl.glClearDepth(1.0);
		_fpgl.glDepthFunc(GL_LESS);
	}
	_fpgl.glDepthMask(GL_TRUE);
	_fpgl.glClear(GL_DEPTH_BUFFER_BIT);

	/* Sky; it covers every pixel, so the colour buffer needs no clearing. */
	_fpgl.glDisable(GL_DEPTH_TEST);
	_fpgl.glDisable(GL_CULL_FACE);
	_fpgl.glUseProgram(this->sky_program);
	const float tan_half_fov = std::tan(static_cast<float>(camera.fov_y) / 2.0f);
	const Vec3f sun = SunDirection();
	_fpgl.glUniform3f(this->sky_uniforms.forward, forward.x, forward.y, forward.z);
	_fpgl.glUniform3f(this->sky_uniforms.right, right.x, right.y, right.z);
	_fpgl.glUniform3f(this->sky_uniforms.up, up.x, up.y, up.z);
	_fpgl.glUniform2f(this->sky_uniforms.tan_half_fov, tan_half_fov * aspect, tan_half_fov);
	_fpgl.glUniform3f(this->sky_uniforms.zenith, SKY_ZENITH.x, SKY_ZENITH.y, SKY_ZENITH.z);
	_fpgl.glUniform3f(this->sky_uniforms.horizon, SKY_HORIZON.x, SKY_HORIZON.y, SKY_HORIZON.z);
	_fpgl.glUniform3f(this->sky_uniforms.ground, SKY_GROUND.x, SKY_GROUND.y, SKY_GROUND.z);
	_fpgl.glUniform3f(this->sky_uniforms.sun_dir, sun.x, sun.y, sun.z);
	_fpgl.glBindVertexArray(this->empty_vertex_array);
	_fpgl.glDrawArrays(GL_TRIANGLES, 0, 3);

	const float tile_length = static_cast<float>(scale.TileLength());
	auto set_lit_uniforms = [&](const LitUniforms &u, float grid_strength) {
		_fpgl.glUniformMatrix4fv(u.view_proj, 1, GL_FALSE, view_proj.m.data());
		_fpgl.glUniform3f(u.sun_dir, sun.x, sun.y, sun.z);
		_fpgl.glUniform3f(u.sun_colour, SUN_COLOUR.x, SUN_COLOUR.y, SUN_COLOUR.z);
		_fpgl.glUniform3f(u.ambient, AMBIENT.x, AMBIENT.y, AMBIENT.z);
		_fpgl.glUniform3f(u.fog_colour, SKY_HORIZON.x, SKY_HORIZON.y, SKY_HORIZON.z);
		_fpgl.glUniform1f(u.fog_density, 1.35f / view_distance);
		_fpgl.glUniform2f(u.grid_offset, static_cast<float>(std::fmod(camera.position.x, scale.TileLength())), static_cast<float>(std::fmod(camera.position.y, scale.TileLength())));
		_fpgl.glUniform1f(u.tile_length, tile_length);
		_fpgl.glUniform1f(u.grid_strength, grid_strength);
	};

	/* Terrain. */
	_fpgl.glEnable(GL_DEPTH_TEST);
	_fpgl.glEnable(GL_CULL_FACE);
	_fpgl.glCullFace(GL_BACK);
	_fpgl.glFrontFace(GL_CCW);
	_fpgl.glUseProgram(this->terrain_program);
	set_lit_uniforms(this->terrain_uniforms, 0.22f);

	/* Side planes of the view frustum, for culling chunks by their bounding sphere. */
	std::array<std::array<float, 4>, 4> planes;
	for (int i = 0; i < 4; i++) {
		int row = i / 2;
		float sign = (i % 2 == 0) ? 1.0f : -1.0f;
		for (int col = 0; col < 4; col++) planes[i][col] = view_proj.At(3, col) + sign * view_proj.At(row, col);
	}

	const double half_chunk = scale.TileLength() * CHUNK_SIZE / 2.0;
	for (const auto &[index, chunk] : this->chunks) {
		if (chunk.index_count == 0) continue;

		Vec3d relative = chunk.origin - camera.position;
		Vec3d centre = relative + Vec3d{half_chunk, half_chunk, (chunk.min_z + chunk.max_z) / 2.0};
		double half_height = (chunk.max_z - chunk.min_z) / 2.0;
		double radius = std::sqrt(2 * half_chunk * half_chunk + half_height * half_height);
		if (std::hypot(centre.x, centre.y) - radius > view_distance) continue;

		bool outside = std::any_of(planes.begin(), planes.end(), [&](const std::array<float, 4> &p) {
			double distance = p[0] * centre.x + p[1] * centre.y + p[2] * centre.z + p[3];
			return distance < -radius * std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
		});
		if (outside) continue;

		_fpgl.glUniform3f(this->terrain_uniforms.offset, static_cast<float>(relative.x), static_cast<float>(relative.y), static_cast<float>(relative.z));
		_fpgl.glBindVertexArray(chunk.vertex_array);
		_fpgl.glDrawElements(GL_TRIANGLES, chunk.index_count, GL_UNSIGNED_SHORT, nullptr);
	}

	/* Vehicles. */
	this->BuildBoxes(camera.position);
	if (!this->boxes.empty()) {
		_fpgl.glDisable(GL_CULL_FACE);
		_fpgl.glUseProgram(this->box_program);
		set_lit_uniforms(this->box_uniforms, 0.0f);
		_fpgl.glBindVertexArray(this->box_vertex_array);
		_fpgl.glBindBuffer(GL_ARRAY_BUFFER, this->box_instance_buffer);
		_fpgl.glBufferData(GL_ARRAY_BUFFER, this->boxes.size() * sizeof(BoxInstance), this->boxes.data(), GL_STREAM_DRAW);
		_fpgl.glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(BOX_INDICES.size()), GL_UNSIGNED_SHORT, nullptr, static_cast<GLsizei>(this->boxes.size()));
	}

	if (this->reversed_z) _fpgl.glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
}

/**
 * Copy the scene onto the visible parts of the view.
 * @param viewport Viewport of the OpenGL back-end, i.e. the whole screen.
 * @param target Framebuffer of the OpenGL back-end.
 */
void Renderer::Composite(const std::array<GLint, 4> &viewport, GLuint target)
{
	_fpgl.glBindFramebuffer(GL_READ_FRAMEBUFFER, this->framebuffer);
	_fpgl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target);
	_fpgl.glDisable(GL_SCISSOR_TEST);

	/* OpenTTD has its origin at the top left, OpenGL at the bottom left. */
	const Rect &view = this->frame.view_rect;
	const int screen_height = viewport[3];
	for (const Rect &r : this->frame.visible) {
		int src_x0 = r.left - view.left;
		int src_x1 = r.right - view.left + 1;
		int src_y0 = this->framebuffer_height - (r.bottom - view.top + 1);
		int src_y1 = this->framebuffer_height - (r.top - view.top);
		int dst_x0 = viewport[0] + r.left;
		int dst_x1 = viewport[0] + r.right + 1;
		int dst_y0 = viewport[1] + screen_height - (r.bottom + 1);
		int dst_y1 = viewport[1] + screen_height - r.top;
		_fpgl.glBlitFramebuffer(src_x0, src_y0, src_x1, src_y1, dst_x0, dst_y0, dst_x1, dst_y1, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	}
}

/** Draw a frame, if there is a view. */
void Renderer::Paint()
{
	ViewHandoff &handoff = Runtime::Get().GetHandoff();
	bool fresh = handoff.Take(this->update);
	if (fresh) this->frame = std::move(this->update.frame);

	if (!this->frame.active) {
		if (this->initialised) this->Shutdown(false);
		this->failed = false;
		this->update.ops.clear();
		if (fresh) handoff.SetRendererStatus({});
		return;
	}

	if (!this->initialised) {
		if (this->failed || !this->Init()) {
			if (!this->failed) Debug(Facility::Fp, Severity::Error, "First-person renderer unavailable: {}", this->info);
			this->failed = true;
			this->update.ops.clear();
			RendererStatus status;
			status.failed = true;
			status.info = this->info;
			handoff.SetRendererStatus(status);
			return;
		}
	}

	auto start = std::chrono::steady_clock::now();
	GLStateGuard guard;

	if (fresh) this->ApplyChunkOps(this->update.reset_chunks, this->update.ops);

	int width = this->frame.view_rect.Width();
	int height = this->frame.view_rect.Height();
	if (!this->frame.visible.empty() && width > 0 && height > 0) {
		this->EnsureFramebuffer(width, height);
		this->DrawScene(width, height);
		this->Composite(guard.GetViewport(), guard.GetDrawFramebuffer());
	}

	RendererStatus status;
	status.running = true;
	status.info = this->info;
	status.chunks = static_cast<uint>(this->chunks.size());
	status.vehicles = static_cast<uint>(this->boxes.size());
	status.frame_ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
	handoff.SetRendererStatus(status);
}

/* Hooks called by the OpenGL back-end; see fp_hooks.h. */

void OnGLPaint()
{
	_renderer.Paint();
}

void OnGLContextDestroy()
{
	_renderer.Shutdown(true);
}

} // namespace fp
