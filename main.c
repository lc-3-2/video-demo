#include "decoder.h"
#include "video.h"

/**
 * \brief Print a string to STDOUT
 * \param s Null-terminated string to print
 */
void puts(const char *s) {
  while (*s) {
    asm volatile("OUT" : : "a"(*s) :);
    s++;
  }
}

#ifndef VIDEO_DEMO_BENCHMARK

#ifndef VIDEO_DEMO_PRESENT_DELAY
/**
 * \brief How many frames to wait between video frames
 *
 * Remember that the player is entirely open loop. Configure this value so you
 * get approximately 15fps on the output.
 */
#define VIDEO_DEMO_PRESENT_DELAY 3
#endif

/**
 * \brief Hardware representation of DMA controller
 */
typedef struct dmactl_t {
  uint32_t src;
  uint32_t dst;
  uint32_t ctl;
} __attribute__((packed)) dmactl_t;

/**
 * \brief Spin until we're in the next VBlank
 */
static void wait_for_vblank(void) {
  // Where to read the current line
  static volatile uint16_t *const REG_VCOUNT = (uint16_t *)0xf0000000;
  // Do the wait
  while (*REG_VCOUNT >= DECODER_HEIGHT)
    ;
  while (*REG_VCOUNT < DECODER_HEIGHT)
    ;
}

/**
 * \brief Return whether the start button is pressed on this frame
 */
static bool start_pressed(void) {
  // Where to read key data
  static volatile uint16_t *const REG_KEYINPUT = (uint16_t *)0xf0000002;
  // Return
  return (*REG_KEYINPUT & (1 << 3)) == 0;
}

/**
 * \brief Return whether the start button was just pressed on this frame
 */
static bool start_newly_pressed(void) {
  // The previous state of the button
  static bool prev = false;
  // Get the current state and compute the return value
  bool cur = start_pressed();
  bool ret = (cur && !prev);
  // Update state and return
  prev = cur;
  return ret;
}

/**
 * \brief If the start button is just pressed, spin until it is pressed again
 */
static void handle_pause(void) {
  if (start_newly_pressed()) {
    while (!start_newly_pressed())
      ;
  }
}

#endif

/**
 * \brief Global decoder for the video
 */
decoder_t decoder;

int main(void) {

#ifndef VIDEO_DEMO_BENCHMARK
  // Relevant MMIO registers
  static volatile dmactl_t *const REG_DMACTL = (dmactl_t *)0xf000000c;
  static volatile uint16_t *const FRAMEBUFFER = (uint16_t *)0xfc000000;
#endif

  // Initialize the decoder
  decoder_initialize(&decoder, video_cvid, video_cvid_len);

  // Continually decode frames
  while (decoder_has_next_frame(&decoder)) {
    // Decode the frame and handle the result
    decoder_status_t r = decoder_compute_frame(&decoder);
    if (r != SUCCESS) {
      puts("Error\n");
      return 1;
    }

#ifndef VIDEO_DEMO_BENCHMARK
    // Wait for VBlank. Skip frames as needed. Also handle pause.
    for (size_t i = 0; i < VIDEO_DEMO_PRESENT_DELAY; i++) {
      wait_for_vblank();
      handle_pause();
    }

    // Blit the framebuffer to the screen
    // We have to do this in multiple passes since we can only transfer 16 bits
    // at a time.
    size_t togo = DECODER_PIXELS;
    size_t done = 0;
    while (togo != 0) {
      // Compute how much to add
      size_t toadd = togo > 0xffff ? 0xffff : togo;
      // Transfer
      REG_DMACTL->src = (intptr_t)(decoder_get_framebuffer(&decoder) + done);
      REG_DMACTL->dst = (intptr_t)(FRAMEBUFFER + done);
      REG_DMACTL->ctl = 0x80000000 | toadd;
      // Update amount to go
      togo -= toadd;
      done += toadd;
    }
#endif
  }
}
