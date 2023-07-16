#include "decoder.h"

// These functions are part of the runtime library, but do not supply headers.
// Therefore, we include them manually.
void *memset(void *s, int c, size_t n);
void *memcpy(void *restrict dest, const void *restrict src, size_t n);

void decoder_initialize(decoder_t *decoder, const void *data, size_t length) {

  // Initialize the data
  decoder->data = data;
  decoder->data_length = length;

  // We start at the start of the data
  decoder->data_index = 0;

  // Clear out all the strips, as well as the framebuffer
  memset(decoder->strips, 0, sizeof(decoder->strips));
  memset(decoder->framebuffer, 0, sizeof(decoder->framebuffer));
}

const uint16_t *decoder_get_framebuffer(const decoder_t *decoder) {
  return decoder->framebuffer;
}

bool decoder_has_next_frame(const decoder_t *decoder) {
  return decoder->data_index < decoder->data_length;
}

#ifdef DECODER_VALIDATE
/**
 * \brief Utility function to get the amount of data remaining
 *
 * Technically this can be exposed to clients, but there's no point in it. It's
 * used when decoding frames to ensure we don't buffer overflow.
 *
 * \param[in] decoder The decoder to check
 * \return How many bytes remain, or zero if we're done
 */
static size_t decoder_data_remaining(const decoder_t *decoder) {
  return decoder_has_next_frame(decoder)
             ? decoder->data_length - decoder->data_index
             : 0;
}
#endif

/**
 * \defgroup DECODER_READ
 * \brief Read various amounts of data from an array
 *
 * Cinepak stores data in big-endian order. Hence, we have these functions to
 * convert data to native integers. We also have a function to read a single
 * byte for consistency. We don't do any overflow checking here, so make sure
 * the data is actually present.
 *
 * @{
 */

static uint8_t read_i8(const unsigned char *data) { return data[0]; }

static uint16_t read_i16(const unsigned char *data) {
  return (data[0] << 8) | (data[1] << 0);
}
static uint32_t read_i32(const unsigned char *data) {
  return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | (data[3] << 0);
}

#ifdef DECODER_VALIDATE
static uint32_t read_i24(const unsigned char *data) {
  return (data[0] << 16) | (data[1] << 8) | (data[2] << 0);
}
#endif
/** @} */

/**
 * \brief Convert CVID YUV to BGR555
 *
 * Note that `u` anv `v` are signed. That's not specified in the format's
 * documentation.
 */
static uint16_t yuv_to_bgr555(uint8_t y, int8_t u, int8_t v) {
  // Convert to higher bits to avoid precision loss
  int_fast16_t yp = y;
  int_fast16_t up = u;
  int_fast16_t vp = v;
  // Do the matrix multiplication
  int_fast16_t rp = yp + vp * 2;
  int_fast16_t gp = yp - up / 2 - vp * 1;
  int_fast16_t bp = yp + up * 2;
  // Clip to range
  uint_fast8_t rc = rp < 0 ? 0 : rp > 255 ? 255 : rp;
  uint_fast8_t gc = gp < 0 ? 0 : gp > 255 ? 255 : gp;
  uint_fast8_t bc = bp < 0 ? 0 : bp > 255 ? 255 : bp;
  // Downsample
  uint16_t rd = rc >> 3;
  uint16_t gd = gc >> 3;
  uint16_t bd = bc >> 3;
  // Assemble and return
  return (bd << 10) | (gd << 5) | (rd << 0);
}

/**
 * \brief Decode four vectors onto the framebuffer
 *
 * This is a very low-level routine, used by the vector decode functions to
 * write data onto the framebuffer. It takes the vectors to write, and it looks
 * them up in the codebook to put the data onto the frame.
 *
 * \param[in] strip Strip to use for the codebooks
 * \param[out] framebuffer Buffer to write to
 * \param[in] vector_entry Four bytes to use to index the V4 table
 * \param[in] y Top of 4x4 block
 * \param[in] x Left of 4x4 block
 */
static void decoder_write_v4(const decoder_strip_t *strip,
                             uint16_t *framebuffer,
                             const unsigned char *vector_entry, uint16_t y,
                             uint16_t x) {
  // Decode (0,0) - (1,1)
  decoder_codebook_t codebook_entry_00 = strip->v4[vector_entry[0]];
  framebuffer[(y + 0) * DECODER_WIDTH + (x + 0)] = codebook_entry_00.c0;
  framebuffer[(y + 0) * DECODER_WIDTH + (x + 1)] = codebook_entry_00.c1;
  framebuffer[(y + 1) * DECODER_WIDTH + (x + 0)] = codebook_entry_00.c2;
  framebuffer[(y + 1) * DECODER_WIDTH + (x + 1)] = codebook_entry_00.c3;
  // Decode (2,0) - (3,1)
  decoder_codebook_t codebook_entry_20 = strip->v4[vector_entry[1]];
  framebuffer[(y + 0) * DECODER_WIDTH + (x + 2)] = codebook_entry_20.c0;
  framebuffer[(y + 0) * DECODER_WIDTH + (x + 3)] = codebook_entry_20.c1;
  framebuffer[(y + 1) * DECODER_WIDTH + (x + 2)] = codebook_entry_20.c2;
  framebuffer[(y + 1) * DECODER_WIDTH + (x + 3)] = codebook_entry_20.c3;
  // Decode (0,2) - (1,3)
  decoder_codebook_t codebook_entry_02 = strip->v4[vector_entry[2]];
  framebuffer[(y + 2) * DECODER_WIDTH + (x + 0)] = codebook_entry_02.c0;
  framebuffer[(y + 2) * DECODER_WIDTH + (x + 1)] = codebook_entry_02.c1;
  framebuffer[(y + 3) * DECODER_WIDTH + (x + 0)] = codebook_entry_02.c2;
  framebuffer[(y + 3) * DECODER_WIDTH + (x + 1)] = codebook_entry_02.c3;
  // Decode (2,2) - (3,3)
  decoder_codebook_t codebook_entry_22 = strip->v4[vector_entry[3]];
  framebuffer[(y + 2) * DECODER_WIDTH + (x + 2)] = codebook_entry_22.c0;
  framebuffer[(y + 2) * DECODER_WIDTH + (x + 3)] = codebook_entry_22.c1;
  framebuffer[(y + 3) * DECODER_WIDTH + (x + 2)] = codebook_entry_22.c2;
  framebuffer[(y + 3) * DECODER_WIDTH + (x + 3)] = codebook_entry_22.c3;
}

/**
 * \brief Decode one vector onto the framebuffer
 * \see decoder_write_v4()
 */
static void decoder_write_v1(const decoder_strip_t *strip,
                             uint16_t *framebuffer,
                             const unsigned char *vector_entry, uint16_t y,
                             uint16_t x) {
  decoder_codebook_t codebook_entry = strip->v1[vector_entry[0]];
  // Decode (0,0) - (1,1)
  framebuffer[(y + 0) * DECODER_WIDTH + (x + 0)] = codebook_entry.c0;
  framebuffer[(y + 0) * DECODER_WIDTH + (x + 1)] = codebook_entry.c0;
  framebuffer[(y + 1) * DECODER_WIDTH + (x + 0)] = codebook_entry.c0;
  framebuffer[(y + 1) * DECODER_WIDTH + (x + 1)] = codebook_entry.c0;
  // Decode (2,0) - (3,1)
  framebuffer[(y + 0) * DECODER_WIDTH + (x + 2)] = codebook_entry.c1;
  framebuffer[(y + 0) * DECODER_WIDTH + (x + 3)] = codebook_entry.c1;
  framebuffer[(y + 1) * DECODER_WIDTH + (x + 2)] = codebook_entry.c1;
  framebuffer[(y + 1) * DECODER_WIDTH + (x + 3)] = codebook_entry.c1;
  // Decode (0,2) - (1,3)
  framebuffer[(y + 2) * DECODER_WIDTH + (x + 0)] = codebook_entry.c2;
  framebuffer[(y + 2) * DECODER_WIDTH + (x + 1)] = codebook_entry.c2;
  framebuffer[(y + 3) * DECODER_WIDTH + (x + 0)] = codebook_entry.c2;
  framebuffer[(y + 3) * DECODER_WIDTH + (x + 1)] = codebook_entry.c2;
  // Decode (2,2) - (3,3)
  framebuffer[(y + 2) * DECODER_WIDTH + (x + 2)] = codebook_entry.c3;
  framebuffer[(y + 2) * DECODER_WIDTH + (x + 3)] = codebook_entry.c3;
  framebuffer[(y + 3) * DECODER_WIDTH + (x + 2)] = codebook_entry.c3;
  framebuffer[(y + 3) * DECODER_WIDTH + (x + 3)] = codebook_entry.c3;
}

/**
 * \brief Decode a stream of bytes representing a codebook
 *
 * The chunk header data is passed via other parameters. As such, the data
 * should not include the chunk header.
 *
 * \param[in] codebook_data Bytes for the codebook, not including the header
 * \param[in] codebook_length Length in bytes, not including the header
 * \param[inout] codebook Codebook to decode into
 * \param[in] bpp12 Whether to use 12bpp or 8bpp mode
 * \param[in] selective Whether to do selective updates
 * \return Whether decoding was successful, and the error if not
 */
static decoder_status_t
decoder_compute_codebook(const unsigned char *codebook_data,
                         size_t codebook_length, decoder_codebook_t *codebook,
                         bool bpp12, bool selective) {

  // Bitmask for which entries to update. This is populated every 32 entries.
  uint32_t update_mask = 0x00000000;

  // Decode all codebook entries
  size_t codebook_index = 0;
  size_t entry_index = 0;
  while (codebook_index < codebook_length) {

#ifdef DECODER_VALIDATE
    // Compute how much data we have remaining
    size_t codebook_remaining = codebook_length - codebook_index;
#endif

    // Get data pointers
    const unsigned char *entry_data = codebook_data + codebook_index;
    decoder_codebook_t *entry = codebook + entry_index;

    // Fetch the new update mask if we have to. We need to repopulate every 32
    // entries.
    if (selective && entry_index % 32 == 0) {
#ifdef DECODER_VALIDATE
      // Check range
      if (codebook_remaining < 4)
        return ERROR_INVALID_DATA;
#endif
      // Update
      update_mask = read_i32(entry_data);
      codebook_index += 4;
      // Recompute
      entry_data += 4;
#ifdef DECODER_VALIDATE
      codebook_remaining -= 4;
#endif
    }

    // Check whether we should skip this block. Clear the update mask to mark
    // we're done
    if (selective) {
      uint32_t skip_mask = 0x80000000 >> (entry_index % 32);
      if ((update_mask & skip_mask) == 0) {
        update_mask &= ~skip_mask;
        continue;
      }
    }

    // Update depending on mode
    if (bpp12) {
#ifdef DECODER_VALIDATE
      // Validate
      if (codebook_remaining < 6)
        return ERROR_INVALID_DATA;
#endif
      // Read
      uint8_t y0 = read_i8(entry_data + 0);
      uint8_t y1 = read_i8(entry_data + 1);
      uint8_t y2 = read_i8(entry_data + 2);
      uint8_t y3 = read_i8(entry_data + 3);
      int8_t u = (int8_t)read_i8(entry_data + 4);
      int8_t v = (int8_t)read_i8(entry_data + 5);
      // Update
      entry->c0 = yuv_to_bgr555(y0, u, v);
      entry->c1 = yuv_to_bgr555(y1, u, v);
      entry->c2 = yuv_to_bgr555(y2, u, v);
      entry->c3 = yuv_to_bgr555(y3, u, v);
      // Next iteration
      codebook_index += 6;

    } else {
#ifdef DECODER_VALIDATE
      // Validate
      if (codebook_remaining < 4)
        return ERROR_INVALID_DATA;
#endif
      // Read
      uint8_t y0 = read_i8(entry_data + 0);
      uint8_t y1 = read_i8(entry_data + 1);
      uint8_t y2 = read_i8(entry_data + 2);
      uint8_t y3 = read_i8(entry_data + 3);
      // Update
      entry->c0 = yuv_to_bgr555(y0, 0, 0);
      entry->c1 = yuv_to_bgr555(y1, 0, 0);
      entry->c2 = yuv_to_bgr555(y2, 0, 0);
      entry->c3 = yuv_to_bgr555(y3, 0, 0);
      // Next iteration
      codebook_index += 4;
    }

    // Next
    entry_index++;
  }

#ifdef DECODER_VALIDATE
  // Check if we ran out of data prematurely. That is, check that we're not
  // supposed to get any more blocks in selective mode
  if (selective && update_mask != 0x00000000)
    return ERROR_INVALID_DATA;
#endif

  return SUCCESS;
}

/**
 * \brief Decode a set of intra-coded vectors
 *
 * This can decode either chunk 0x3000 or 0x3200. The chunk header data is
 * passed via other parameters, so the data and length should not include the
 * header data.
 *
 * \param[in] vector_data The data for the vectors
 * \param[in] vector_length How long the vector data is
 * \param[in] strip The strip with the coordinates and codebook
 * \param[out] framebuffer The frame to write into
 * \param[in] mixed Whether we have mixed V4 and V1 or only V1
 * \return Whether decoding was successful, and the error if not
 */
static decoder_status_t decoder_compute_intra_vectors(
    const unsigned char *vector_data, size_t vector_length,
    const decoder_strip_t *strip, uint16_t *framebuffer, bool mixed) {

#ifndef DECODER_VALIDATE
  (void)vector_length;
#endif

  // Mask for V4/V1 disambiguation. This is only used in mixed mode, and it's
  // populated every 32 pixels.
  uint32_t v4_mask = 0x00000000;

  // Iterate over the frame. We're guaranteed that the strip has boundaries on
  // multiples of four
  size_t vector_index = 0;
  size_t pixel_index = 0;
  for (uint16_t y = strip->y0; y < strip->y1; y += 4) {
    for (uint16_t x = strip->x0; x < strip->x1; x += 4) {

#ifdef DECODER_VALIDATE
      // Check we have enough data
      if (vector_index > vector_length)
        return ERROR_INVALID_DATA;
      size_t vector_remaining = vector_length - vector_index;
#endif

      // Compute where we are
      const unsigned char *vector_entry = vector_data + vector_index;

      // If we need to repopulate v4mask, do so
      if (mixed && pixel_index % 32 == 0) {
#ifdef DECODER_VALIDATE
        // Check we have enough space
        if (vector_remaining < 4)
          return ERROR_INVALID_DATA;
#endif
        // Read
        v4_mask = read_i32(vector_entry);
        vector_index += 4;
        // Recompute
        vector_entry += 4;
#ifdef DECODER_VALIDATE
        vector_remaining -= 4;
#endif
      }

      // Figure out what mode we're in
      bool v4 = false;
      if (mixed) {
        uint32_t cur_mask = 0x80000000 >> (pixel_index % 32);
        if ((v4_mask & cur_mask) != 0)
          v4 = true;
      }

      if (v4) {
#ifdef DECODER_VALIDATE
        // Check we have enough data
        if (vector_remaining < 4)
          return ERROR_INVALID_DATA;
#endif
        // Decode
        decoder_write_v4(strip, framebuffer, vector_entry, y, x);
        // Next
        vector_index += 4;

      } else {
#ifdef DECODER_VALIDATE
        // Check we have enough data
        if (vector_remaining < 1)
          return ERROR_INVALID_DATA;
#endif
        // Decode
        decoder_write_v1(strip, framebuffer, vector_entry, y, x);
        // Next
        vector_index += 1;
      }

      // Next
      pixel_index++;
    }
  }

#ifdef DECODER_VALIDATE
  // Check we consumed all the data
  if (vector_index != vector_length)
    return ERROR_INVALID_DATA;
#endif

  return SUCCESS;
}

/**
 * \brief Decode a set of inter-coded vectors
 * \see decoder_compute_intra_vectors()
 */
static decoder_status_t decoder_compute_inter_vectors(
    const unsigned char *vector_data, size_t vector_length,
    const decoder_strip_t *strip, uint16_t *framebuffer) {

#ifndef DECODER_VALIDATE
  (void)vector_length;
#endif

  // Mask for our "instructions". These tell us whether to skip a block or how
  // to interpret it if we're decoding it. Also keep track of how many positions
  // we've read.
  uint32_t instr_mask = 0x00000000;
  size_t instr_index = 0;

  // Iterate over the frame. We're guaranteed that the strip has boundaries on
  // multiples of four
  size_t vector_index = 0;
  for (uint16_t y = strip->y0; y < strip->y1; y += 4) {
    for (uint16_t x = strip->x0; x < strip->x1; x += 4) {

#ifdef DECODER_VALIDATE
      // Check we have enough data
      if (vector_index > vector_length)
        return ERROR_INVALID_DATA;
      size_t vector_remaining = vector_length - vector_index;
#endif

      // Compute where we are
      const unsigned char *vector_entry = vector_data + vector_index;

      // Decode the instruction
      uint_fast8_t instr = 0;
      for (size_t i = 0; i < 2; i++) {
        // Read in more instructions if we have to
        if (instr_index % 32 == 0) {
#ifdef DECODER_VALIDATE
          // Check we have enough space
          if (vector_remaining < 4)
            return ERROR_INVALID_DATA;
#endif
          // Read
          instr_mask = read_i32(vector_entry);
          vector_index += 4;
          // Recompute
          vector_entry += 4;
#ifdef DECODER_VALIDATE
          vector_remaining -= 4;
#endif
        }
        // Shift in the new bit
        uint32_t bit_mask = 0x80000000 >> (instr_index % 32);
        instr_index++;
        instr <<= 1;
        if ((instr_mask & bit_mask) != 0)
          instr |= 1;
        // If the first bit was zero, break
        if (instr == 0)
          break;
      }

#ifdef DECODER_VALIDATE
      // Check we got valid data
      if (instr != 0b0 && instr != 0b10 && instr != 0b11)
        return ERROR_INTERNAL;
#endif

      // Check if we should skip this block
      if (instr == 0b0)
        continue;

      if (instr == 0b11) {
#ifdef DECODER_VALIDATE
        // Check we have enough data
        if (vector_remaining < 4)
          return ERROR_INVALID_DATA;
#endif
        // Decode
        decoder_write_v4(strip, framebuffer, vector_entry, y, x);
        // Next
        vector_index += 4;

      } else if (instr == 0b10) {
#ifdef DECODER_VALIDATE
        // Check we have enough data
        if (vector_remaining < 1)
          return ERROR_INVALID_DATA;
#endif
        // Decode
        decoder_write_v1(strip, framebuffer, vector_entry, y, x);
        // Next
        vector_index += 1;
      }
    }
  }

#ifdef DECODER_VALIDATE
  // Check we consumed all the data
  if (vector_index != vector_length)
    return ERROR_INVALID_DATA;
#endif

  return SUCCESS;
}

/**
 * \brief Decode a single strip
 * \param[in] strip_data Bytes corresponding to strip, starting at header
 * \param[in] strip_length Number of valid bytes in `strip_data`
 * \param[in] strip_current Strip to decode into
 * \param[in] strip_previous Previous strip decoded, or `NULL`
 * \param[inout] framebuffer Buffer to decode into
 * \param[in] frame_inter_coded Whether to puse previous strip's codebook
 * \return Whether decoding was successful, and the error if not
 */
static decoder_status_t
decoder_compute_strip(const unsigned char *strip_data, size_t strip_length,
                      decoder_strip_t *strip_current,
                      const decoder_strip_t *strip_previous,
                      uint16_t *framebuffer, bool frame_inter_coded) {

  // Read the dimensions
  strip_current->x0 = read_i16(strip_data + 6);
  strip_current->x1 = read_i16(strip_data + 10);
  strip_current->y0 = read_i16(strip_data + 4);
  strip_current->y1 = read_i16(strip_data + 8);
#ifdef DECODER_VALIDATE
  // Validate. We don't handle strips that don't end on a multiple of four
  if (strip_current->x1 > DECODER_WIDTH || strip_current->y1 > DECODER_HEIGHT)
    return ERROR_INVALID_DATA;
  if (strip_current->x0 % 4 != 0 || strip_current->x1 % 4 != 0 ||
      strip_current->y0 % 4 != 0 || strip_current->y1 % 4 != 0)
    return ERROR_INVALID_DATA;
  if (strip_current->x0 >= strip_current->x1 ||
      strip_current->y0 >= strip_current->y1)
    return ERROR_INVALID_DATA;
#endif

#ifdef DECODER_VALIDATE
  // Check that the strip ID only takes valid values. This isn't actually used
  // for anything though. We can get all chunk types regardless of how this
  // frame is coded.
  uint16_t strip_id = read_i16(strip_data + 0);
  if (strip_id != 0x1000 && strip_id != 0x1100)
    return ERROR_INVALID_DATA;
#endif

#ifdef DECODER_VALIDATE
  // Sanity check the input length. This should already be correct.
  if (strip_length != read_i16(strip_data + 2))
    return ERROR_INTERNAL;
#endif

  // If our y0 is zero, that actually means that it's relative to the previous
  // strip (if the previous strip exists)
  if (strip_current->y0 == 0 && strip_previous != NULL) {
    strip_current->y0 = strip_previous->y1;
    strip_current->y1 = strip_previous->y1 + strip_current->y1;
  }

  // If the frame is inter-coded, that means we should use the previous strips
  // codebooks (if the previous strip exists)
  if (frame_inter_coded && strip_previous != NULL) {
    memcpy(strip_current->v1, strip_previous->v1, sizeof(strip_current->v1));
    memcpy(strip_current->v4, strip_previous->v4, sizeof(strip_current->v4));
  }

  // Process each chunk. Remember to skip the header data
  for (size_t chunk_index = 12; chunk_index != strip_length;) {
    const unsigned char *chunk_data = strip_data + chunk_index;

#ifdef DECODER_VALIDATE
    // Make sure the chunk header exists
    if (chunk_index + 4 > strip_length)
      return ERROR_INVALID_DATA;
#endif
    // Read the chunk header
    uint16_t chunk_id = read_i16(chunk_data + 0);
    size_t chunk_length = read_i16(chunk_data + 2);

#ifdef DECODER_VALIDATE
    // Check the length is good
    if (chunk_length < 4)
      return ERROR_INVALID_DATA;
    if (chunk_index + chunk_length > strip_length)
      return ERROR_INVALID_DATA;
#endif

    // Decode specific chunk types
    decoder_status_t r;
    switch (chunk_id) {
    default:
      return ERROR_INVALID_DATA;

    case 0x2000:
    case 0x2100:
    case 0x2200:
    case 0x2300:
    case 0x2400:
    case 0x2500:
    case 0x2600:
    case 0x2700: {
      // Figure out what codebook to decode into
      decoder_codebook_t *codebook =
          (chunk_id & 0x0200) != 0 ? strip_current->v1 : strip_current->v4;
      // Compute the other parameters
      bool bpp12 = (chunk_id & 0x0400) == 0;
      bool selective = (chunk_id & 0x0100) != 0;
      // Decode
      r = decoder_compute_codebook(chunk_data + 4, chunk_length - 4, codebook,
                                   bpp12, selective);
      break;
    }

    case 0x3000:
    case 0x3200: {
      // Figure out whether we have mixed vectors or not
      bool mixed = (chunk_id & 0x0200) == 0;
      // Decode
      r = decoder_compute_intra_vectors(chunk_data + 4, chunk_length - 4,
                                        strip_current, framebuffer, mixed);
      break;
    }

    case 0x3100: {
      r = decoder_compute_inter_vectors(chunk_data + 4, chunk_length - 4,
                                        strip_current, framebuffer);
      break;
    }
    }

    // Check success
    if (r != SUCCESS)
      return r;

    // Done
    chunk_index += chunk_length;
  }

  return SUCCESS;
}

decoder_status_t decoder_compute_frame(decoder_t *decoder) {

  // Check we have data to decode
  if (!decoder_has_next_frame(decoder))
    return ERROR_EOF;

#ifdef DECODER_VALIDATE
  // Check that we have a frame header
  if (decoder_data_remaining(decoder) < 10)
    return ERROR_INVALID_DATA;
#endif

  // Check the dimensions of the frame
  const unsigned char *const frame_data = decoder->data + decoder->data_index;
#ifdef DECODER_VALIDATE
  const size_t frame_width = read_i16(frame_data + 4);
  const size_t frame_height = read_i16(frame_data + 6);
  if (frame_width != DECODER_WIDTH || frame_height != DECODER_HEIGHT)
    return ERROR_BAD_DIMENSIONS;
#endif

  // Pull out the other data
  const bool frame_inter_coded = (read_i8(frame_data + 0) & 0x01) == 0;
  const size_t frame_strips = read_i16(frame_data + 8);

#ifdef DECODER_VALIDATE
  // Get the frame length for error checking. Note that the frame length
  // includes the header.
  const size_t frame_length = read_i24(frame_data + 1);
  if (frame_length < 10)
    return ERROR_INVALID_DATA;
#endif

  // Done with the frame header
  decoder->data_index += 10;

#ifdef DECODER_VALIDATE
  // Validate number of strips
  if (frame_strips > DECODER_MAX_STRIPS)
    return ERROR_INVALID_DATA;
#endif
  // Provide a fast track if no strips
  if (frame_strips == 0)
    return SUCCESS;

  // Decode all the strips
  for (size_t i = 0; i < frame_strips; i++) {

#ifdef DECODER_VALIDATE
    // Check we have enough space for at least the strip header
    if (decoder_data_remaining(decoder) < 12)
      return ERROR_INVALID_DATA;
#endif
    // Pull out where we are currently in the data
    decoder_strip_t *const strip_current = decoder->strips + i;
    const unsigned char *const strip_data = decoder->data + decoder->data_index;

    // Read the size of the strip. This includes the size of the header
    size_t strip_length = read_i16(strip_data + 2);
#ifdef DECODER_VALIDATE
    // Validate
    if (strip_length < 12)
      return ERROR_INVALID_DATA;
    if (decoder_data_remaining(decoder) < strip_length)
      return ERROR_INVALID_DATA;
#endif

    // Get the previous strip we used if possible. The strip decoder uses this
    // information for codebooks and coordinates.
    const decoder_strip_t *const strip_previous =
        i > 0 ? decoder->strips + (i - 1) : NULL;

    // Try decode
    decoder_status_t r = decoder_compute_strip(
        strip_data, strip_length, strip_current, strip_previous,
        decoder->framebuffer, frame_inter_coded);
    if (r != SUCCESS)
      return r;

    // If it worked, go to the next strip
    decoder->data_index += strip_length;
  }

#ifdef DECODER_VALIDATE
  // Check that we ended up where we should've ended up
  if (decoder->data + decoder->data_index != frame_data + frame_length)
    return ERROR_INVALID_DATA;
#endif

  return SUCCESS;
}
