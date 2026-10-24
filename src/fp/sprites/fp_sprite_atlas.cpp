/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_sprite_atlas.cpp Atlas of sprite images for the GPU. */

#include "../../stdafx.h"
#include "fp_sprite_atlas.h"
#include "fp_sprite_bridge.h"
#include "../../core/math_func.hpp"
#include "../../debug.h"

#include "../../safeguards.h"

namespace fp {

/** Passes of colour bleeding; enough to colour every transparent texel a tile shares a block of the last mipmap level with. */
static constexpr uint BLEED_PASSES = ATLAS_ALIGN;

/**
 * Append changes that happened after these.
 * @param later The later changes; emptied.
 */
void AtlasChanges::Merge(AtlasChanges &&later)
{
	if (later.reset) {
		this->uploads.clear();
		this->reset = true;
	}
	std::move(later.uploads.begin(), later.uploads.end(), std::back_inserter(this->uploads));
	this->pages = later.pages;
	later = {};
}

/**
 * Find room for a rectangle.
 * @param width Width, in texels.
 * @param height Height, in texels.
 * @return Where it goes, or std::nullopt if all pages are full.
 */
std::optional<AtlasPacker::Slot> AtlasPacker::Allocate(uint width, uint height)
{
	if (width == 0 || height == 0 || width > ATLAS_PAGE_SIZE || height > ATLAS_PAGE_SIZE) return std::nullopt;

	Shelf *best = nullptr;
	for (Shelf &shelf : this->shelves) {
		if (shelf.height < height || ATLAS_PAGE_SIZE - shelf.used < width) continue;
		if (best == nullptr || shelf.height < best->height) best = &shelf;
	}

	/* In a much taller shelf the space above the rectangle is lost; rather open a new shelf, while there is room. */
	if (best != nullptr && best->height - height > height / 2) {
		if (auto slot = this->OpenShelf(width, height); slot.has_value()) return slot;
	}
	if (best == nullptr) return this->OpenShelf(width, height);

	Slot slot{best->page, best->used, best->y};
	best->used += width;
	this->used_area += static_cast<uint64_t>(width) * height;
	return slot;
}

/**
 * Open a shelf for a rectangle, on the first page with room, or on a new page.
 * @param width Width of the rectangle, in texels.
 * @param height Height of the rectangle and the shelf, in texels.
 * @return Where the rectangle goes, or std::nullopt if no page has room.
 */
std::optional<AtlasPacker::Slot> AtlasPacker::OpenShelf(uint width, uint height)
{
	uint page = 0;
	while (page < this->page_tops.size() && this->page_tops[page] + height > ATLAS_PAGE_SIZE) page++;
	if (page == this->page_tops.size()) {
		if (page == ATLAS_MAX_PAGES) return std::nullopt;
		this->page_tops.push_back(0);
	}

	Shelf &shelf = this->shelves.emplace_back(page, this->page_tops[page], height, width);
	this->page_tops[page] += height;
	this->used_area += static_cast<uint64_t>(width) * height;
	return Slot{page, 0, shelf.y};
}

/** Free all pages. */
void AtlasPacker::Reset()
{
	this->shelves.clear();
	this->page_tops.clear();
	this->used_area = 0;
}

/**
 * Convert an sRGB channel to linear light.
 * @param value The channel.
 * @return Linear value from 0 to 1.
 */
static float SrgbToLinear(uint8_t value)
{
	static const std::array<float, 256> table = []() {
		std::array<float, 256> t{};
		for (uint i = 0; i < t.size(); i++) {
			double c = i / 255.0;
			t[i] = static_cast<float>(c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4));
		}
		return t;
	}();
	return table[value];
}

/**
 * Convert linear light to an sRGB channel.
 * @param value Linear value from 0 to 1.
 * @return The channel.
 */
static uint8_t LinearToSrgb(float value)
{
	double c = Clamp(static_cast<double>(value), 0.0, 1.0);
	c = c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
	return static_cast<uint8_t>(std::lround(c * 255.0));
}

/**
 * Give transparent texels the colour of their visible neighbours.
 * Filtering mixes the colour of transparent texels into the edges of the
 * image; without this they would get a dark fringe.
 * @param[in,out] pixels The texels.
 * @param width Width.
 * @param height Height.
 */
static void BleedColours(std::vector<Rgba8> &pixels, uint width, uint height)
{
	std::vector<uint8_t> known(pixels.size());
	uint64_t sum_r = 0, sum_g = 0, sum_b = 0, count = 0;
	for (size_t i = 0; i < pixels.size(); i++) {
		if (pixels[i].a == 0) continue;
		known[i] = 1;
		sum_r += pixels[i].r;
		sum_g += pixels[i].g;
		sum_b += pixels[i].b;
		count++;
	}
	if (count == 0 || count == pixels.size()) return;

	std::vector<uint8_t> next = known;
	for (uint pass = 0; pass < BLEED_PASSES; pass++) {
		bool changed = false;
		for (uint y = 0; y < height; y++) {
			for (uint x = 0; x < width; x++) {
				size_t i = static_cast<size_t>(y) * width + x;
				if (known[i] != 0) continue;

				uint r = 0, g = 0, b = 0, n = 0;
				for (uint ny = (y > 0 ? y - 1 : 0); ny <= std::min(y + 1, height - 1); ny++) {
					for (uint nx = (x > 0 ? x - 1 : 0); nx <= std::min(x + 1, width - 1); nx++) {
						size_t j = static_cast<size_t>(ny) * width + nx;
						if (known[j] == 0) continue;
						r += pixels[j].r;
						g += pixels[j].g;
						b += pixels[j].b;
						n++;
					}
				}
				if (n == 0) continue;
				pixels[i] = {static_cast<uint8_t>(r / n), static_cast<uint8_t>(g / n), static_cast<uint8_t>(b / n), 0};
				next[i] = 1;
				changed = true;
			}
		}
		if (!changed) break;
		known = next;
	}

	Rgba8 average{static_cast<uint8_t>(sum_r / count), static_cast<uint8_t>(sum_g / count), static_cast<uint8_t>(sum_b / count), 0};
	for (size_t i = 0; i < pixels.size(); i++) {
		if (known[i] == 0) pixels[i] = average;
	}
}

/**
 * Halve a mipmap level. Colours are averaged in linear light and weighted
 * by alpha, which is what premultiplied alpha would give.
 * @param source The level, \a width by \a height texels.
 * @param width Width of the level; even.
 * @param height Height of the level; even.
 * @param[out] out Receives the next level, appended.
 */
static void HalveLevel(std::span<const Rgba8> source, uint width, uint height, std::vector<Rgba8> &out)
{
	for (uint y = 0; y < height; y += 2) {
		for (uint x = 0; x < width; x += 2) {
			std::array<const Rgba8 *, 4> quad = {
				&source[static_cast<size_t>(y) * width + x], &source[static_cast<size_t>(y) * width + x + 1],
				&source[static_cast<size_t>(y + 1) * width + x], &source[static_cast<size_t>(y + 1) * width + x + 1],
			};

			float alpha = 0, r = 0, g = 0, b = 0, plain_r = 0, plain_g = 0, plain_b = 0;
			for (const Rgba8 *p : quad) {
				float w = p->a / 255.0f;
				alpha += w;
				r += SrgbToLinear(p->r) * w;
				g += SrgbToLinear(p->g) * w;
				b += SrgbToLinear(p->b) * w;
				plain_r += SrgbToLinear(p->r);
				plain_g += SrgbToLinear(p->g);
				plain_b += SrgbToLinear(p->b);
			}

			uint sum_a = quad[0]->a + quad[1]->a + quad[2]->a + quad[3]->a;
			Rgba8 &texel = out.emplace_back();
			texel.a = static_cast<uint8_t>((sum_a + 2) / 4);
			if (alpha > 0) {
				texel.r = LinearToSrgb(r / alpha);
				texel.g = LinearToSrgb(g / alpha);
				texel.b = LinearToSrgb(b / alpha);
			} else {
				texel.r = LinearToSrgb(plain_r / 4);
				texel.g = LinearToSrgb(plain_g / 4);
				texel.b = LinearToSrgb(plain_b / 4);
			}
		}
	}
}

/**
 * Prepare an image for the atlas: add a gutter that repeats its edge, give
 * transparent texels the colour of their surroundings and build all mipmap
 * levels. Position and size of the tile are left to the caller.
 * @param image The image.
 * @param[out] upload Receives the size and the pixels of the tile.
 */
void BuildAtlasTile(const SpriteImage &image, AtlasUpload &upload)
{
	const uint width = Align(image.width + 2 * ATLAS_GUTTER, ATLAS_ALIGN);
	const uint height = Align(image.height + 2 * ATLAS_GUTTER, ATLAS_ALIGN);
	upload.width = static_cast<uint16_t>(width);
	upload.height = static_cast<uint16_t>(height);

	size_t total = 0;
	for (uint level = 0; level < ATLAS_MIP_LEVELS; level++) total += static_cast<size_t>(width >> level) * (height >> level);
	upload.pixels.clear();
	upload.pixels.reserve(total);
	upload.pixels.resize(static_cast<size_t>(width) * height);

	if (image.width > 0 && image.height > 0) {
		for (uint y = 0; y < height; y++) {
			int sy = Clamp(static_cast<int>(y) - static_cast<int>(ATLAS_GUTTER), 0, image.height - 1);
			for (uint x = 0; x < width; x++) {
				int sx = Clamp(static_cast<int>(x) - static_cast<int>(ATLAS_GUTTER), 0, image.width - 1);
				upload.pixels[static_cast<size_t>(y) * width + x] = image.At(sx, sy);
			}
		}
		BleedColours(upload.pixels, width, height);
	}

	size_t offset = 0;
	for (uint level = 1; level < ATLAS_MIP_LEVELS; level++) {
		uint w = width >> (level - 1);
		uint h = height >> (level - 1);
		/* The vector does not grow beyond its reserved size, so the span stays valid. */
		HalveLevel(std::span<const Rgba8>(upload.pixels).subspan(offset, static_cast<size_t>(w) * h), w, h, upload.pixels);
		offset += static_cast<size_t>(w) * h;
	}
}

/**
 * Length of a sprite pixel in the world, chosen so that the diamond of a
 * flat tile in the 2D view is as wide as the diagonal of a tile in metres.
 * @param scale The metric scales.
 * @param zoom Zoom level of the pixel.
 * @return Length, in metres.
 */
double SpritePixelLength(const MetricScale &scale, ZoomLevel zoom)
{
	const double diamond_pixels = 2.0 * TILE_PIXELS * ZOOM_BASE; // At ZoomLevel::Min.
	return scale.TileLength() * std::numbers::sqrt2 / diamond_pixels * ScaleByZoom(1, zoom);
}

/**
 * Get the region of a sprite image, putting it into the atlas if needed.
 * If the atlas is full it starts over, which voids all regions taken
 * before; check #GetEpoch() after a series of requests.
 * @param sprite The sprite; modifier bits are handled like DrawSprite() does.
 * @param palette The palette.
 * @return The region, or std::nullopt if the sprite cannot be shown; see SpriteBridge::CheckRequest().
 */
std::optional<AtlasRegion> SpriteAtlas::Request(SpriteID sprite, PaletteID palette)
{
	SpriteBridge &bridge = SpriteBridge::Get();
	if (uint32_t generation = bridge.GetGeneration(); generation != this->generation) {
		this->Reset();
		this->generation = generation;
	}

	NormaliseSpriteRequest(sprite, palette);
	uint64_t key = (static_cast<uint64_t>(sprite) << 32) | palette;
	if (auto it = this->regions.find(key); it != this->regions.end()) return it->second;

	auto image = bridge.GetImage(sprite, palette);
	if (image == nullptr) {
		this->regions[key] = std::nullopt;
		return std::nullopt;
	}

	AtlasUpload upload;
	BuildAtlasTile(*image, upload);
	auto slot = this->packer.Allocate(upload.width, upload.height);
	if (!slot.has_value() && upload.width <= ATLAS_PAGE_SIZE && upload.height <= ATLAS_PAGE_SIZE) {
		Debug(Facility::Fp, Severity::Info, "Sprite atlas full with {} images; starting over", this->regions.size());
		this->Reset();
		slot = this->packer.Allocate(upload.width, upload.height);
	}
	if (!slot.has_value()) {
		Debug(Facility::Fp, Severity::Error, "Sprite {} is too large for the atlas: {}x{} pixels", sprite, image->width, image->height);
		this->regions[key] = std::nullopt;
		return std::nullopt;
	}

	AtlasRegion region;
	static_cast<SpriteGeometry &>(region) = *image;
	region.page = static_cast<uint16_t>(slot->page);
	region.x = static_cast<uint16_t>(slot->x + ATLAS_GUTTER);
	region.y = static_cast<uint16_t>(slot->y + ATLAS_GUTTER);

	upload.page = region.page;
	upload.x = static_cast<uint16_t>(slot->x);
	upload.y = static_cast<uint16_t>(slot->y);
	this->uploaded_bytes += upload.pixels.size() * sizeof(Rgba8);
	this->pending.uploads.push_back(std::move(upload));
	this->pending.pages = this->packer.GetPageCount();

	this->regions[key] = region;
	return region;
}

/** Empty the atlas and start a new epoch. */
void SpriteAtlas::Reset()
{
	this->packer.Reset();
	this->regions.clear();
	this->pending.uploads.clear();
	this->pending.reset = true;
	this->pending.pages = 0;
	this->epoch++;
}

/**
 * Take the changes since the last call, for the renderer.
 * @param[out] changes Receives the changes.
 */
void SpriteAtlas::TakeChanges(AtlasChanges &changes)
{
	changes = std::move(this->pending);
	this->pending = {};
	this->pending.pages = this->packer.GetPageCount();
}

/**
 * Counters of the atlas.
 * @return The counters.
 */
SpriteAtlas::Statistics SpriteAtlas::GetStatistics() const
{
	Statistics statistics;
	statistics.images = static_cast<uint>(std::count_if(this->regions.begin(), this->regions.end(), [](const auto &entry) { return entry.second.has_value(); }));
	statistics.pages = this->packer.GetPageCount();
	statistics.used_area = this->packer.GetUsedArea();
	statistics.epoch = this->epoch;
	statistics.uploaded_bytes = this->uploaded_bytes;
	return statistics;
}

} // namespace fp
