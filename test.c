/**
 * \file test.c
 * \brief Test harness for the decoder code
 *
 * This is a very minimal test harness for the cinepak decoder. It should be
 * compiled and run on the host, and it's meant to validate whether the decoder
 * is coded correctly.
 *
 * It loads a video into memory, then generates frames from it and writes them
 * out as NetPBM images.
 *
 * Its first argument is the input file in raw CVID format - without the
 * container. It writes files into a hardcoded directory.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>

#include "decoder.h"

/**
 * \defgroup CONFIG
 * \brief Configuration parameters
 *
 * These really should be command-line arguments, but I just couldn't be
 * bothered to parse them.
 * @{
 */

/**
 * \brief What directory to write output files to
 *
 * This directory must exist.
 */
const char *const OUT_DIR = "test-out";

/**
 * \brief How many frames to skip between writes
 *
 * This way, we don't write every frame as an image and take up a ton of disk
 * space.
 */
const size_t OUT_INTERVAL = 30;

/** @} */

/**
 * \brief Print an error message, then exit
 * \param[in] msg The message to print to STDERR
 */
__attribute__((noreturn)) void die(const char *msg) {
  fprintf(stderr, "Error: %s\n", msg);
  exit(1);
}

/**
 * \brief Represents a buffer of known length
 *
 * This exists purely for us to read files with read_video(). We need to pass
 * the length information back to the main method somehow, and this is how we do
 * that.`
 */
typedef struct buffer_t {
  void *data;
  size_t length;
} buffer_t;

/**
 * \brief Read a video file into memory
 *
 * If an error occurs during reading, this method calls die() and exits.
 * Otherwise, it returns an allocated buffer containing the contents of the file
 * whose name was passed in.
 *
 * \param[in] video_name The file to read from
 * \return A buffer representing the video
 */
buffer_t read_video(const char *video_name) {

  // Open the file
  FILE *video_handle;
  {
    video_handle = fopen(video_name, "r");
    if (video_handle == NULL)
      die("failed to open video file");
  }

  // Get the file length
  buffer_t video;
  {
    int err;
    // Go to end
    err = fseek(video_handle, 0l, SEEK_END);
    if (err != 0)
      die("failed to seek in video file");
    // Get offset
    video.length = ftell(video_handle);
    // Rewind
    err = fseek(video_handle, 0l, SEEK_SET);
    if (err != 0)
      die("failed to seek in video file");
  }

  // Read the file into memory
  {
    // Allocate
    video.data = malloc(video.length);
    if (video.data == NULL)
      die("failed to allocate video buffer");
    // Read
    size_t r = fread(video.data, 1, video.length, video_handle);
    if (r != video.length)
      die("failed to read video file");
  }

  // Done with the file
  fclose(video_handle);
  // Return
  return video;
}

/**
 * \brief Write the data in the framebuffer to a file
 *
 * If an error occurs, this method calls die() and exits.
 *
 * \param[in] frame The frame to write in RGB555
 * \param[in] frame_name The filename to write
 */
void write_framebuffer(const uint16_t *frame, const char *frame_name) {

  // Variable to store the result of converting the frame to RGB888
  static uint8_t frame_converted[3 * DECODER_PIXELS];
  // Convert the frame
  for (size_t i = 0; i < DECODER_PIXELS; i++) {
    frame_converted[3 * i + 0] = ((frame[i] >> 0) & 0x1f) << 3;
    frame_converted[3 * i + 1] = ((frame[i] >> 5) & 0x1f) << 3;
    frame_converted[3 * i + 2] = ((frame[i] >> 10) & 0x1f) << 3;
  }

  // Open the file
  FILE *frame_handle;
  {
    frame_handle = fopen(frame_name, "w");
    if (frame_handle == NULL)
      die("failed to open frame for writing");
  }

  // Write the header
  {
    int err =
        fprintf(frame_handle, "P6 %d %d 255\n", DECODER_WIDTH, DECODER_HEIGHT);
    if (err < 0)
      die("failed to write header");
  }

  // Write the data
  {
    size_t w = fwrite(frame_converted, 1, 3 * DECODER_PIXELS, frame_handle);
    if (w != 3 * DECODER_PIXELS)
      die("failed to write data");
  }

  // Done
  fclose(frame_handle);
}

/**
 * \brief Global decoder for cinepak
 */
decoder_t decoder;

int main(int argc, char **argv) {

  // We expect exactly one argument
  if (argc != 2)
    die("need exactly one argument");

  // Read in the video
  buffer_t video = read_video(argv[1]);
  printf("Successfully read %s (%zu bytes)\n", argv[1], video.length);

  // Initialize the decoder
  decoder_initialize(&decoder, video.data, video.length);
  printf("Successfully initialized decoder\n");

  // Get frames
  size_t i = 0;
  while (decoder_has_next_frame(&decoder)) {
    // Decode the frame and handle the result
    decoder_status_t r = decoder_compute_frame(&decoder);
    if (r != SUCCESS)
      die("got error after decoding");

    // Only write every so often
    if (i % OUT_INTERVAL == 0) {
      // Compute the filename to write
      char *fn;
      if (asprintf(&fn, "%s/%zu.ppm", OUT_DIR, i) < 0)
        die("failed to allocate filename");
      // Get the framebuffer to write
      const uint16_t *fb = decoder_get_framebuffer(&decoder);
      // Write it
      write_framebuffer(fb, fn);
      // Done
      free(fn);
      printf("Successfully wrote frame %zu\n", i);
    }

    // Remember to increment
    i++;
  }

  // Done
  free(video.data);
}
