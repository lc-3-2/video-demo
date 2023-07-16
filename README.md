# LC-3.2 Video Demo

A small video player for the LC-3.2. It exists as a test of the simulator's
display and button capabilities. It plays a video baked into the executable
image, and it supports pausing via the start button.

This was tested with commit `fe2c719e3cb134ef781a5b1c75801685f3840c3c` of
`lc32sim`. It may break with changing MMIO configuration.

## Encoding

In order to play a video on the LC-3.2, you need a video to play. Specifically,
it needs to be raw [Cinepak][1] data, with the video being 320x240@15fps. Given
an `.mp4` file, you can convert it to this format via:
```bash
$ ffmpeg \
  -i input.mp4 \
  -s "320x240" -r 15 -c:v cinepak -f rawvideo \
  -q:v 256 -an \
  output.cvid
```
If the input's aspect ratio is not 4:3, use the crop filter. Additionally, the
video quality is tunable via `-q:v`. Smaller values produce better quality video
at the cost of a larger file size. I found `256` to work well, but that can be
increased or decreased.

[1]: https://en.wikipedia.org/wiki/Cinepak "Wikipedia: Cinepak"

### Versions

This code was built and tested against the following version of `ffpmeg`:
```
ffmpeg version n6.0 Copyright (c) 2000-2023 the FFmpeg developers
built with gcc 13.1.1 (GCC) 20230429
configuration: --prefix=/usr --disable-debug --disable-static --disable-stripping --enable-amf --enable-avisynth --enable-cuda-llvm --enable-lto --enable-fontconfig --enable-gmp --enable-gnutls --enable-gpl --enable-ladspa --enable-libaom --enable-libass --enable-libbluray --enable-libbs2b --enable-libdav1d --enable-libdrm --enable-libfreetype --enable-libfribidi --enable-libgsm --enable-libiec61883 --enable-libjack --enable-libjxl --enable-libmfx --enable-libmodplug --enable-libmp3lame --enable-libopencore_amrnb --enable-libopencore_amrwb --enable-libopenjpeg --enable-libopenmpt --enable-libopus --enable-libpulse --enable-librav1e --enable-librsvg --enable-libsoxr --enable-libspeex --enable-libsrt --enable-libssh --enable-libsvtav1 --enable-libtheora --enable-libv4l2 --enable-libvidstab --enable-libvmaf --enable-libvorbis --enable-libvpx --enable-libwebp --enable-libx264 --enable-libx265 --enable-libxcb --enable-libxml2 --enable-libxvid --enable-libzimg --enable-nvdec --enable-nvenc --enable-opencl --enable-opengl --enable-shared --enable-version3 --enable-vulkan
libavutil      58.  2.100 / 58.  2.100
libavcodec     60.  3.100 / 60.  3.100
libavformat    60.  3.100 / 60.  3.100
libavdevice    60.  1.100 / 60.  1.100
libavfilter     9.  3.100 /  9.  3.100
libswscale      7.  1.100 /  7.  1.100
libswresample   4. 10.100 /  4. 10.100
libpostproc    57.  1.100 / 57.  1.100
```

## Building

Building the LC-3.2 program is done with `make`. When invoking `make`, be sure
to set the variable `VIDEO_DEMO_CVID` to the path to the Cinepak file generated
in the previous step. The `Makefile` will complain if you don't do this.

Additionally, you can set the `CDEFS` variable to pass additional defines to the
code. The following are recognized:
* `DECODER_VALIDATE`: Add error checking to the Cinepak decoder. This is not
  needed if the data is known to be good. I recommend building and running the
  test harness with this option (which is the default). This way, you can test
  the input data beforehand and not have to use this define on the device.
* `VIDEO_DEMO_PRESENT_DELAY`: How many frames to delay before presenting the
  next frame. The video player is entirely open loop, so set this to something
  that gets about 15fps. Default is `3`.
* `VIDEO_DEMO_BENCHMARK`: For the demo, only run the code that decodes the
  frame. That is, don't present it to the screen. This can be useful for
  benchmarking.

### Running

The code expects a 320x240 screen, and it expects to operate at a higher speed
than the default. Use the provided `lc32sim.json`.

## Testing

A test harness is provided to validate the Cinepak decoder. It runs on the host,
and can be built as
```bash
$ make -f test.mak
```
It takes a single command-line argument: the raw Cinepak data to process. It
outputs ever `30`-th frame to the `test-out` directory, but these can be changed
by modifying `test.c`. Note that `test-out` must exist on program startup.
