# mkv-writer

Small, dependency-free Matroska writer for a single encoded video track and an
optional uncompressed audio track. It streams encoded packets directly to a
seekable file and back-patches cluster and segment sizes during finalization,
avoiding whole-GOP buffering.

The initial API supports:

- one video track;
- an optional interleaved 32-bit float PCM audio track (`A_PCM/FLOAT/IEEE`);
- arbitrary Matroska codec ID and codec-private bytes (used with AV1 and HEVC);
- `SimpleBlock` output with millisecond timestamps;
- keyframe cues and a `SeekHead`;
- bounded five-second clusters;
- static builds on C++17 toolchains.

Compressed audio, subtitles, lacing, chapters, tags, attachments, and non-seekable
outputs are intentionally outside this library's scope.

## Usage

```cpp
#include <MkvWriter.h>

#include <fstream>

mkv_writer::MkvWriter writer;
writer.Open(std::ofstream("capture.mkv", std::ios::binary),
            1920, 1080, 60, 1, "V_AV1");  // frame rate as a rational, e.g. 24000, 1001
writer.SetCodecPrivate(codec_private.data(), codec_private.size());
writer.SetAudioTrack(48000, 2);  // optional
writer.WriteFrame(reinterpret_cast<const std::byte*>(packet.data()),
                  packet.size(), timestamp_ms, is_keyframe);
writer.WriteAudio(samples.data(), sample_frame_count, timestamp_ms);
writer.Finalize();
```

Every mutating operation returns `bool`; inspect `LastError()` after a failure.
Rejected arguments or call ordering do not poison otherwise valid output, while
stream and structural errors do. Call `Finalize()` explicitly to observe fatal
or finalization failures. The writer also finalizes an open file from its
destructor as a safety net.

## Build and test

```console
cmake -S . -B build -DMKV_WRITER_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Use the library directly from another CMake project:

```cmake
add_subdirectory(path/to/mkv-writer)
target_link_libraries(my-recorder PRIVATE mkv-writer::mkv-writer)
```

Or install it and consume the exported package:

```console
cmake --install build --config Release --prefix install
```

```cmake
find_package(mkv-writer CONFIG REQUIRED)
target_link_libraries(my-recorder PRIVATE mkv-writer::mkv-writer)
```

## Conan

The repository includes a Conan 2 recipe for the static library. Create the
package and run its consumer test with:

```console
conan profile detect --force
conan create conan -s build_type=Release -s compiler.cppstd=17 --build=missing
```

Consume `mkv-writer/0.2.0` through `CMakeDeps` and link the same
`mkv-writer::mkv-writer` target shown above.

## License

MIT. See [LICENSE](LICENSE).
