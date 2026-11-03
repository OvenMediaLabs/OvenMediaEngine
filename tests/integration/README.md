# Local recording integration test

The built-in GoogleTest suite checks Writer timestamps with MP4, FLV and MPEGTS
muxers. This optional test runs OME with a 16-second H.264/AAC source using two
B-frames and closed GOPs, ending on an IDR. It uses a five-second discontinuous
recording interval and checks segment keyframes, A/V decoding, video packet/frame counts and
contiguous source frames, including the finalized last file.

Requirements: Linux, Python 3, `ffmpeg` and `ffprobe` on PATH, with libx264/AAC
encoding and RTMP support. The test uses loopback only; no external service,
downloads or containers are required. External FFmpeg 6.1.1 was used for validation.

```sh
cmake -S . -B build -DOME_BUILD_TESTS=ON -DOME_BUILD_INTEGRATION_TESTS=ON
cmake --build build --target OvenMediaEngine tests
ctest --test-dir build -L integration --output-on-failure
```

The test takes about 20 seconds and needs roughly 20 MB of temporary disk space.
It runs one OME process and one FFmpeg process at a time, using two dynamically
selected TCP ports. Another process claiming a selected port before OME binds it
can cause a startup failure.

The runner terminates and reaps its own processes on completion, interruption or
its 90-second deadline. Temporary files are removed on success; failure artifacts
are retained in the reported directory. To retain successful captures too, run
the script directly with `--ome build/bin/OvenMediaEngine --artifact-dir <existing-directory>`.

This test covers FILE recording, not push reconnects, other recording formats or
lossless input shutdown or compatibility with every player. Zero-composition-offset
and single-track cases remain in the built-in Writer tests.
