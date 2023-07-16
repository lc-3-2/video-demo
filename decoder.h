/**
 * \file decoder.h
 * \brief Decode raw cinepak data to BGR555 frames
 *
 * The interface presented in this file expects the input to be raw CVID data,
 * meaning no containers like AVI. It expects it to be 320x240, and it should be
 * at 15fps (though frame synchronization must be handled by the player). The
 * library decodes individual frames into BGR555 format. It then exposes a
 * reference to the current framebuffer.
 *
 * Cinepak is a proprietary format, so there's not much documentation to go off
 * of. The main sources are: [Ferguson][1] and the [FFMPEG Source][2]. FFMPEG is
 * taken to be the reference implementation. If it differs from the
 * specification I follow that.
 *
 * [1]: https://multimedia.cx/mirror/cinepak.txt
 * [2]: https://github.com/FFmpeg/FFmpeg/blob/release/6.0/libavcodec/cinepak.c
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * \defgroup DECODER_DIMENSIONS
 * \brief Dimensions of the screen, in pixels
 * @{
 */
#define DECODER_WIDTH 320
#define DECODER_HEIGHT 240
#define DECODER_PIXELS (320 * 240)
/** @} */

/**
 * \brief Maximum number of codebook entries per strip
 *
 * This is limited by the way we index entries. We have one byte to designate a
 * codebook entry, so we can have at most 256 V1 and V4 entries.
 */
#define DECODER_MAX_ENTRIES 256

/**
 * \brief Maximum number of strips in a frame
 *
 * FFMPEG caps it at 32, so we do too.
 */
#define DECODER_MAX_STRIPS 32

/**
 * \brief A single codebook entry
 *
 * Recall that each strip in CVID has a V1 and a V4 codebook associated with it.
 * Each entry in those codebooks has four luminance entries and two chrominance
 * entries. However, these are stored decoded as RGB555.
 *
 * Note that this structure is used for *both* the V1 and V4 codebooks, even
 * though it has slightly different meanings for them.
 */
typedef struct decoder_codebook_t {
  uint16_t c0;
  uint16_t c1;
  uint16_t c2;
  uint16_t c3;
} decoder_codebook_t;

/**
 * \brief Structure containing the state for a single strip
 *
 * Each strip has its own dimensions, and maintains its own codebooks. This
 * struct encapsulates that data.
 */
typedef struct decoder_strip_t {

  uint16_t x0;
  uint16_t x1;
  uint16_t y0;
  uint16_t y1;

  decoder_codebook_t v4[DECODER_MAX_ENTRIES];
  decoder_codebook_t v1[DECODER_MAX_ENTRIES];

} decoder_strip_t;

/**
 * \brief Top-level state for the decoder
 *
 * This keeps track of the data, as well as where we are inside it. It also
 * holds all the strips, as well as the current framebuffer.
 */
typedef struct decoder_t {

  const unsigned char *data;
  size_t data_index;
  size_t data_length;

  decoder_strip_t strips[DECODER_MAX_STRIPS];
  uint16_t framebuffer[DECODER_WIDTH * DECODER_HEIGHT];

} decoder_t;

/**
 * \brief Initialize a decoder with video data
 *
 * Any decoder must be initialized before using it to decode frames.
 *
 * \param[in] decoder The structure to initialize
 * \param[in] data The data to decode
 * \param[in] length The length of the data in bytes
 */
void decoder_initialize(decoder_t *decoder, const void *data, size_t length);

/**
 * \brief Get a reference to a decoder's framebuffer
 * \return `decoder->framebuffer`
 */
const uint16_t *decoder_get_framebuffer(const decoder_t *decoder);

/**
 * \brief Whether we can compute the next frame
 * \return If another frame exists after the last one computed
 */
bool decoder_has_next_frame(const decoder_t *decoder);

/**
 * \brief The possible results of decoding a frame
 */
typedef enum decoder_status_t {
  SUCCESS,
  ERROR_EOF,
  ERROR_INVALID_DATA,
  ERROR_BAD_DIMENSIONS,
  ERROR_INTERNAL,
} decoder_status_t;

/**
 * \brief Compute the next frame
 *
 * The result of the computation goes into the internal framebuffer. It can be
 * accessed with decoder_get_framebuffer(). Calling this method invalidates the
 * previous pointers gotten through decoder_get_framebuffer().
 */
decoder_status_t decoder_compute_frame(decoder_t *decoder);
