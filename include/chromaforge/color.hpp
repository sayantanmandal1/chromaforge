// chromaforge/color.hpp — color-space conversion and grading operators.
#pragma once

#include "chromaforge/image.hpp"
#include "chromaforge/thread_pool.hpp"

namespace chromaforge {
namespace color {

// --- Color-space conversion (BT.601 full-range, 0..255 in/out) ---
void rgbToYCbCr(float r, float g, float b, float& y, float& cb, float& cr);
void ycbcrToRgb(float y, float cb, float cr, float& r, float& g, float& b);

// --- In-place grading operators (parallelized by row) ---

// contrast is a multiplier about mid-gray (1.0 = unchanged); brightness is an
// additive offset in [-1, 1] scaled to the 0..255 range.
void brightnessContrast(Image& img, ThreadPool& pool, float brightness, float contrast);

// out = 255 * (in/255) ^ (1/gamma). gamma > 1 brightens midtones.
void gamma(Image& img, ThreadPool& pool, float gammaValue);

// Exposure in photographic stops: out = in * 2^stops.
void exposure(Image& img, ThreadPool& pool, float stops);

// Saturation around per-pixel luma (Rec.601 weights). 0 = grayscale, 1 = same.
void saturation(Image& img, ThreadPool& pool, float amount);

// ASC-CDL 3-way corrector, per channel: out = (in * gain + lift) ^ (1/gamma).
void liftGammaGain(Image& img, ThreadPool& pool, const float lift[3],
                   const float gammaC[3], const float gain[3]);

// --- Transfer functions & HDR-style tone mapping (processed in linear light) ---

// sRGB electro-optical / opto-electronic transfer functions on the 0..1 domain.
float srgbToLinear(float c);
float linearToSrgb(float c);

// White balance by independent channel gains from a temperature shift
// (-1..1, warm/cool) and tint (-1..1, green/magenta).
void whiteBalance(Image& img, ThreadPool& pool, float temperature, float tint);

// Tone mapping performed in linear light: linearize sRGB, apply an exposure
// gain (in stops), compress highlights, then re-encode to sRGB.
void toneMapReinhard(Image& img, ThreadPool& pool, float exposureStops);
void toneMapACES(Image& img, ThreadPool& pool, float exposureStops);  // Narkowicz fit

// Convert to a single-channel luma image.
Image toGrayscale(const Image& src);

}  // namespace color
}  // namespace chromaforge
