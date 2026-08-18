#pragma once

#include "mkv_writer/Export.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mkv_writer {

// Minimal Matroska muxer for one hardware-encoded video track. Headers are
// written lazily so codec-private bytes discovered in the first packet can be
// supplied before the first WriteFrame call.
class MkvWriter final
{
public:
    MKV_WRITER_API MkvWriter();
    MKV_WRITER_API ~MkvWriter() noexcept;

    MkvWriter( const MkvWriter& ) = delete;
    MkvWriter& operator=( const MkvWriter& ) = delete;
    MkvWriter( MkvWriter&& ) = delete;
    MkvWriter& operator=( MkvWriter&& ) = delete;

    // Takes ownership of an open binary, seekable output stream. Append mode
    // is unsupported because finalization back-patches earlier metadata.
    MKV_WRITER_API bool Open( std::ofstream&& outStream,
                              const std::uint32_t width,
                              const std::uint32_t height,
                              const float fps,
                              const std::string_view codecId );

    // Must be called after Open and before the first WriteFrame.
    MKV_WRITER_API bool SetCodecPrivate( const std::uint8_t* const data,
                                         const std::size_t size );

    MKV_WRITER_API bool WriteFrame( const void* const data,
                                    const std::size_t size,
                                    const std::uint64_t timestampMs,
                                    const bool keyframe );

    // Patches sizes, duration and cue metadata, then closes the file.
    // It is safe to call more than once.
    MKV_WRITER_API bool Finalize();

    [[nodiscard]] MKV_WRITER_API bool IsOpen() const noexcept;
    [[nodiscard]] MKV_WRITER_API std::uint64_t GetWrittenFrameCount() const noexcept;
    [[nodiscard]] MKV_WRITER_API std::string_view LastError() const noexcept;

private:
    bool WriteHeaders();
    bool FlushCluster();
    bool PatchSize8( const std::streampos offset, const std::uint64_t size );
    bool WriteBytes( const void* const data,
                     const std::size_t size,
                     const std::string_view operation );
    bool Fail( std::string message );

    std::ofstream file;

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    float fps = 60.0f;
    std::string codecId;
    std::vector< std::uint8_t > codecPrivate;

    bool headersWritten = false;
    std::uint64_t frameCount = 0;
    std::uint64_t lastTimestampMs = 0;

    // Blocks stream directly to disk; only their small EBML header is buffered.
    std::vector< std::uint8_t > blockHeader;
    std::uint64_t clusterStartMs = 0;
    bool clusterOpen = false;

    // Keyframe timestamp and cluster position relative to the Segment payload.
    std::vector< std::pair< std::uint64_t, std::uint64_t > > cuePoints;

    std::streampos segmentSizeOffset{};
    std::streampos segmentPayloadStart{};
    std::streampos durationPayloadOffset{};
    std::streampos seekHeadOffset{};
    std::streampos clusterSizeOffset{};

    std::string lastError;
};

}  // namespace mkv_writer
