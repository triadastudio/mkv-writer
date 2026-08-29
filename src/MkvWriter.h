#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mkv_writer {

// Minimal Matroska muxer for one encoded video track and an optional
// uncompressed audio track. Headers are written lazily so codec-private bytes
// discovered in the first packet can be supplied before the first write call.
class MkvWriter final
{
public:
    MkvWriter();
    ~MkvWriter() noexcept;

    MkvWriter( const MkvWriter& ) = delete;
    MkvWriter& operator=( const MkvWriter& ) = delete;
    MkvWriter( MkvWriter&& ) = delete;
    MkvWriter& operator=( MkvWriter&& ) = delete;

    // Takes ownership of an open binary, seekable output stream. Append mode
    // is unsupported because finalization back-patches earlier metadata. The
    // frame rate is the rational fpsNum / fpsDen; integer-rate callers pass
    // the rate and 1.
    bool Open( std::ofstream&& outStream,
               const std::uint32_t width,
               const std::uint32_t height,
               const std::uint32_t fpsNum,
               const std::uint32_t fpsDen,
               const std::string_view codecId );

    // Must be called after Open and before the first WriteFrame.
    bool SetCodecPrivate( const std::uint8_t* const data,
                          const std::size_t size );

    // Enables a second track carrying interleaved 32-bit float PCM
    // (A_PCM/FLOAT/IEEE). Must be called after Open and before the first write
    // on either track.
    bool SetAudioTrack( const std::uint32_t sampleRate,
                        const std::uint32_t channelCount );

    bool WriteFrame( const std::byte* const data,
                     const std::size_t size,
                     const std::uint64_t timestampMs,
                     const bool keyframe );

    // Interleaved float sample frames for the track enabled by SetAudioTrack.
    // Writes must be near video order: a block may trail the open cluster start
    // by at most 32 seconds. Like WriteFrame this triggers the lazy header
    // write, so supply the video codec-private bytes before audio leads video.
    bool WriteAudio( const float* const samples,
                     const std::size_t sampleFrameCount,
                     const std::uint64_t timestampMs );

    // Patches sizes, duration and cue metadata, then closes the file. Returns
    // false after a fatal stream or structural error, but not after a rejected
    // call that left the output intact. It is safe to call more than once.
    bool Finalize();

    [[nodiscard]] bool IsOpen() const noexcept;
    [[nodiscard]] std::uint64_t GetWrittenFrameCount() const noexcept;

    // Inspect after an operation returns false. The view can be invalidated by
    // a later non-const operation.
    [[nodiscard]] std::string_view LastError() const noexcept;

private:
    bool WriteHeaders();
    bool FlushCluster();
    bool EnsureCluster( const std::uint64_t timestampMs, const bool cueKeyframe );
    bool WriteSimpleBlock( const std::uint8_t trackNumber,
                           const std::byte* const data,
                           const std::size_t size,
                           const std::uint64_t timestampMs,
                           const bool keyframe,
                           const std::string_view operation );
    bool PatchSize8( const std::streampos offset, const std::uint64_t size );
    bool WriteBytes( const void* const data,
                     const std::size_t size,
                     const std::string_view operation );
    bool Reject( std::string message );
    bool Fail( std::string message );

    std::ofstream file;

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t fpsNum = 60u;
    std::uint32_t fpsDen = 1u;
    std::string codecId;
    std::vector< std::uint8_t > codecPrivate;

    bool headersWritten = false;
    std::uint64_t frameCount = 0;
    std::uint64_t lastTimestampMs = 0;

    // Zero sample rate means no audio track.
    std::uint32_t audioSampleRate = 0;
    std::uint32_t audioChannelCount = 0;
    std::uint64_t audioFrameCount = 0;
    std::uint64_t lastAudioTimestampMs = 0;
    std::uint64_t audioEndMs = 0;

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
    bool hasFatalError = false;
};

}  // namespace mkv_writer
