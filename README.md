# mkv-writer

Small, dependency-free Matroska writer for a single hardware-encoded video track.
It streams encoded packets directly to a seekable file and back-patches cluster and
segment sizes during finalization, avoiding whole-GOP buffering.

The initial API supports:

- one video track;
- arbitrary Matroska codec ID and codec-private bytes (used with AV1 and HEVC);
- `SimpleBlock` output with millisecond timestamps;
- keyframe cues and a `SeekHead`;
- bounded five-second clusters;
- static builds on C++17 toolchains.

Audio, subtitles, lacing, chapters, tags, attachments, and non-seekable outputs are
intentionally outside this library's scope.

## Usage

```cpp
#include <MkvWriter.h>

#include <fstream>

mkv_writer::MkvWriter writer;
writer.Open(std::ofstream("capture.mkv", std::ios::binary),
            1920, 1080, 60.0f, "V_AV1");
writer.SetCodecPrivate(codec_private.data(), codec_private.size());
writer.WriteFrame(packet.data(), packet.size(), timestamp_ms, is_keyframe);
writer.Finalize();
```

Every mutating operation returns `bool`; `LastError()` reports the first failure.
Call `Finalize()` explicitly to observe finalization failures. The writer also
finalizes an open file from its destructor as a safety net.

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

## License

MIT. See [LICENSE](LICENSE).
