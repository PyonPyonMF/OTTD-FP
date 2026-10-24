/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_shaders.h GLSL sources of the first-person renderer. */

#ifndef FP_SHADERS_H
#define FP_SHADERS_H

namespace fp {

/*
 * All positions reaching the shaders are relative to the camera, which keeps
 * single precision accurate on maps that are a hundred kilometres across.
 * Colours are given in sRGB, lit in linear space and converted back to sRGB
 * at the end, as the framebuffer holds sRGB values.
 */

/** Sky: a full-screen triangle with a gradient from the horizon to the zenith and a sun. */
static constexpr std::string_view SKY_VERTEX_SHADER = R"glsl(#version 330 core
out vec2 v_ndc;
void main()
{
	vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;
	v_ndc = p;
	gl_Position = vec4(p, 0.0, 1.0);
}
)glsl";

/** @copydoc SKY_VERTEX_SHADER */
static constexpr std::string_view SKY_FRAGMENT_SHADER = R"glsl(#version 330 core
in vec2 v_ndc;
uniform vec3 u_forward;
uniform vec3 u_right;
uniform vec3 u_up;
uniform vec2 u_tan_half_fov;
uniform vec3 u_zenith;
uniform vec3 u_horizon;
uniform vec3 u_ground;
uniform vec3 u_sun_dir;
out vec4 o_colour;
void main()
{
	vec3 dir = normalize(u_forward + u_right * (v_ndc.x * u_tan_half_fov.x) + u_up * (v_ndc.y * u_tan_half_fov.y));
	vec3 zenith = pow(u_zenith, vec3(2.2));
	vec3 horizon = pow(u_horizon, vec3(2.2));
	vec3 ground = pow(u_ground, vec3(2.2));
	vec3 sky = dir.z >= 0.0 ? mix(horizon, zenith, pow(dir.z, 0.45)) : mix(horizon, ground, pow(-dir.z, 0.35));
	float sun = max(dot(dir, u_sun_dir), 0.0);
	sky += vec3(1.0, 0.92, 0.78) * (pow(sun, 900.0) * 6.0 + pow(sun, 12.0) * 0.12);
	o_colour = vec4(pow(sky, vec3(1.0 / 2.2)), 1.0);
}
)glsl";

/** Terrain: chunks of the world mirror, positioned relative to the camera through u_offset. */
static constexpr std::string_view TERRAIN_VERTEX_SHADER = R"glsl(#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec4 a_colour;
uniform mat4 u_view_proj;
uniform vec3 u_offset;
out vec3 v_rel;
out vec3 v_colour;
void main()
{
	v_rel = a_position + u_offset;
	v_colour = a_colour.rgb;
	gl_Position = u_view_proj * vec4(v_rel, 1.0);
}
)glsl";

/** Vehicles: instanced boxes standing on the centre of their base. */
static constexpr std::string_view BOX_VERTEX_SHADER = R"glsl(#version 330 core
layout(location = 0) in vec3 a_corner;
layout(location = 1) in vec4 a_centre_yaw;
layout(location = 2) in vec3 a_size;
layout(location = 3) in vec4 a_colour;
uniform mat4 u_view_proj;
out vec3 v_rel;
out vec3 v_colour;
void main()
{
	vec3 p = a_corner * a_size;
	float c = cos(a_centre_yaw.w);
	float s = sin(a_centre_yaw.w);
	v_rel = a_centre_yaw.xyz + vec3(c * p.x - s * p.y, s * p.x + c * p.y, p.z);
	v_colour = a_colour.rgb;
	gl_Position = u_view_proj * vec4(v_rel, 1.0);
}
)glsl";

/**
 * Sprites as billboards: upright quads that turn about the vertical axis to
 * face the camera. The quad spans the image in metres around its anchor.
 */
static constexpr std::string_view BILLBOARD_VERTEX_SHADER = R"glsl(#version 330 core
layout(location = 0) in vec3 a_anchor;
layout(location = 1) in vec4 a_rect;
layout(location = 2) in vec4 a_uv;
uniform mat4 u_view_proj;
out vec3 v_rel;
out vec2 v_uv;
void main()
{
	vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
	float distance = length(a_anchor.xy);
	vec2 right = distance > 1e-3 ? vec2(a_anchor.y, -a_anchor.x) / distance : vec2(1.0, 0.0);
	float across = mix(a_rect.x, a_rect.y, corner.x);
	float up = mix(a_rect.z, a_rect.w, corner.y);
	v_rel = a_anchor + vec3(right * across, up);
	v_uv = vec2(mix(a_uv.x, a_uv.z, corner.x), mix(a_uv.w, a_uv.y, corner.y));
	gl_Position = u_view_proj * vec4(v_rel, 1.0);
}
)glsl";

/**
 * Sprites as billboards. The atlas is sRGB, so the texture already gives
 * linear colours. Sprites carry their own light, so they are not lit again;
 * pixels less than half covered are dropped.
 *
 * Seen from close by, a sprite pixel covers many screen pixels. Plain
 * bilinear filtering would blur the pixel art, nearest filtering would make
 * its edges crawl; so the coordinates snap to the texel centre and blend
 * only across the last screen pixel before the next texel. From afar the
 * mipmaps take over as usual.
 */
static constexpr std::string_view BILLBOARD_FRAGMENT_SHADER = R"glsl(#version 330 core
in vec3 v_rel;
in vec2 v_uv;
uniform sampler2D u_atlas;
uniform vec3 u_fog_colour;
uniform float u_fog_density;
out vec4 o_colour;
void main()
{
	vec2 size = vec2(textureSize(u_atlas, 0));
	vec2 position = v_uv * size;
	vec2 seam = floor(position + 0.5);
	position = seam + clamp((position - seam) / max(fwidth(position), vec2(1e-5)), -0.5, 0.5);
	vec4 texel = textureGrad(u_atlas, position / size, dFdx(v_uv), dFdy(v_uv));
	if (texel.a < 0.5) discard;
	float fog = 1.0 - exp(-pow(length(v_rel) * u_fog_density, 2.0));
	vec3 colour = mix(texel.rgb, pow(u_fog_colour, vec3(2.2)), fog);
	o_colour = vec4(pow(colour, vec3(1.0 / 2.2)), 1.0);
}
)glsl";

/**
 * Shared lighting of terrain and boxes. The normal comes from the screen-space
 * derivatives of the position, which gives flat shading without normals in
 * the vertex data; it always faces the viewer. On terrain, u_grid_strength
 * draws the tile grid, anti-aliased and faded out with distance.
 */
static constexpr std::string_view LIT_FRAGMENT_SHADER = R"glsl(#version 330 core
in vec3 v_rel;
in vec3 v_colour;
uniform vec3 u_sun_dir;
uniform vec3 u_sun_colour;
uniform vec3 u_ambient;
uniform vec3 u_fog_colour;
uniform float u_fog_density;
uniform vec2 u_grid_offset;
uniform float u_tile_length;
uniform float u_grid_strength;
out vec4 o_colour;
void main()
{
	vec3 n = normalize(cross(dFdx(v_rel), dFdy(v_rel)));
	vec3 albedo = pow(v_colour, vec3(2.2));
	float diffuse = max(dot(n, u_sun_dir), 0.0);
	vec3 lit = albedo * (u_ambient + u_sun_colour * diffuse);

	float dist = length(v_rel);
	if (u_grid_strength > 0.0) {
		vec2 g = (v_rel.xy + u_grid_offset) / u_tile_length;
		vec2 w = max(fwidth(g), vec2(1e-5));
		vec2 d = abs(fract(g - 0.5) - 0.5) / w;
		float line = 1.0 - clamp(min(d.x, d.y), 0.0, 1.0);
		line *= clamp(1.0 - max(w.x, w.y) * 3.0, 0.0, 1.0);
		lit *= 1.0 - u_grid_strength * line;
	}

	float fog = 1.0 - exp(-pow(dist * u_fog_density, 2.0));
	vec3 colour = mix(lit, pow(u_fog_colour, vec3(2.2)), fog);
	o_colour = vec4(pow(colour, vec3(1.0 / 2.2)), 1.0);
}
)glsl";

} // namespace fp

#endif /* FP_SHADERS_H */
