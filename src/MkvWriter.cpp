#include "MkvWriter.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace mkv_writer {

namespace {

using ByteBuffer = std::vector< std::uint8_t >;

constexpr std::uint32_t IdEbml = 0x1A45DFA3u;
constexpr std::uint32_t IdEbmlVersion = 0x4286u;
constexpr std::uint32_t IdEbmlReadVersion = 0x42F7u;
constexpr std::uint32_t IdEbmlMaxIdLength = 0x42F2u;
constexpr std::uint32_t IdEbmlMaxSizeLength = 0x42F3u;
constexpr std::uint32_t IdDocType = 0x4282u;
constexpr std::uint32_t IdDocTypeVersion = 0x4287u;
constexpr std::uint32_t IdDocTypeReadVersion = 0x4285u;
constexpr std::uint32_t IdSegment = 0x18538067u;
constexpr std::uint32_t IdInfo = 0x1549A966u;
constexpr std::uint32_t IdTimestampScale = 0x2AD7B1u;
constexpr std::uint32_t IdMuxingApp = 0x4D80u;
constexpr std::uint32_t IdWritingApp = 0x5741u;
constexpr std::uint32_t IdDuration = 0x4489u;
constexpr std::uint32_t IdTracks = 0x1654AE6Bu;
constexpr std::uint32_t IdTrackEntry = 0xAEu;
constexpr std::uint32_t IdTrackNumber = 0xD7u;
constexpr std::uint32_t IdTrackUid = 0x73C5u;
constexpr std::uint32_t IdTrackType = 0x83u;
constexpr std::uint32_t IdFlagLacing = 0x9Cu;
constexpr std::uint32_t IdCodecId = 0x86u;
constexpr std::uint32_t IdCodecPrivate = 0x63A2u;
constexpr std::uint32_t IdDefaultDuration = 0x23E383u;
constexpr std::uint32_t IdVideo = 0xE0u;
constexpr std::uint32_t IdPixelWidth = 0xB0u;
constexpr std::uint32_t IdPixelHeight = 0xBAu;
constexpr std::uint32_t IdAudio = 0xE1u;
constexpr std::uint32_t IdSamplingFrequency = 0xB5u;
constexpr std::uint32_t IdChannels = 0x9Fu;
constexpr std::uint32_t IdBitDepth = 0x6264u;
constexpr std::uint32_t IdCluster = 0x1F43B675u;
constexpr std::uint32_t IdClusterTimestamp = 0xE7u;
constexpr std::uint32_t IdSimpleBlock = 0xA3u;
constexpr std::uint32_t IdSeekHead = 0x114D9B74u;
constexpr std::uint32_t IdSeek = 0x4DBBu;
constexpr std::uint32_t IdSeekId = 0x53ABu;
constexpr std::uint32_t IdSeekPosition = 0x53ACu;
constexpr std::uint32_t IdVoid = 0xECu;
constexpr std::uint32_t IdCues = 0x1C53BB6Bu;
constexpr std::uint32_t IdCuePoint = 0xBBu;
constexpr std::uint32_t IdCueTime = 0xB3u;
constexpr std::uint32_t IdCueTrackPositions = 0xB7u;
constexpr std::uint32_t IdCueTrack = 0xF7u;
constexpr std::uint32_t IdCueClusterPosition = 0xF1u;

constexpr std::size_t SeekHeadBytes = 26;
constexpr std::uint64_t TimestampScaleNs = 1'000'000;
constexpr std::uint64_t MaxClusterDurationMs = 5'000;
constexpr std::uint64_t MaxKnownSize8 = ( 1ull << 56u ) - 2u;

void AppendId( ByteBuffer& out, const std::uint32_t id )
{
    std::uint32_t byteCount = 4;
    while( byteCount > 1 && ( id >> ( ( byteCount - 1 ) * 8 ) ) == 0 )
        --byteCount;

    for( std::uint32_t i = byteCount; i > 0; --i )
        out.push_back( static_cast< std::uint8_t >( id >> ( ( i - 1 ) * 8 ) ) );
}

void AppendSize( ByteBuffer& out, const std::uint64_t size )
{
    std::uint32_t byteCount = 1;
    while( byteCount < 8 && size + 1 >= ( 1ull << ( 7 * byteCount ) ) )
        ++byteCount;

    const std::uint64_t encoded = ( 1ull << ( 7 * byteCount ) ) | size;
    for( std::uint32_t i = byteCount; i > 0; --i )
        out.push_back( static_cast< std::uint8_t >( encoded >> ( ( i - 1 ) * 8 ) ) );
}

void AppendUintElement( ByteBuffer& out, const std::uint32_t id, const std::uint64_t value )
{
    std::uint32_t byteCount = 1;
    while( byteCount < 8 && ( value >> ( byteCount * 8 ) ) != 0 )
        ++byteCount;

    AppendId( out, id );
    AppendSize( out, byteCount );
    for( std::uint32_t i = byteCount; i > 0; --i )
        out.push_back( static_cast< std::uint8_t >( value >> ( ( i - 1 ) * 8 ) ) );
}

void AppendStringElement( ByteBuffer& out, const std::uint32_t id, const std::string_view value )
{
    AppendId( out, id );
    AppendSize( out, value.size() );
    out.insert( out.end(), value.begin(), value.end() );
}

void AppendBinaryElement( ByteBuffer& out,
                          const std::uint32_t id,
                          const ByteBuffer& data )
{
    AppendId( out, id );
    AppendSize( out, data.size() );
    out.insert( out.end(), data.begin(), data.end() );
}

void AppendFloat8Element( ByteBuffer& out, const std::uint32_t id, const double value )
{
    std::uint64_t bits = 0;
    std::memcpy( &bits, &value, sizeof( bits ) );

    AppendId( out, id );
    AppendSize( out, 8 );
    for( std::uint32_t i = 8; i > 0; --i )
        out.push_back( static_cast< std::uint8_t >( bits >> ( ( i - 1 ) * 8 ) ) );
}

void AppendMasterElement( ByteBuffer& out,
                          const std::uint32_t id,
                          const ByteBuffer& payload )
{
    AppendId( out, id );
    AppendSize( out, payload.size() );
    out.insert( out.end(), payload.begin(), payload.end() );
}

void AppendUintElementFixed8( ByteBuffer& out,
                              const std::uint32_t id,
                              const std::uint64_t value )
{
    AppendId( out, id );
    AppendSize( out, 8 );
    for( std::uint32_t i = 8; i > 0; --i )
        out.push_back( static_cast< std::uint8_t >( value >> ( ( i - 1 ) * 8 ) ) );
}

}  // namespace

MkvWriter::MkvWriter() = default;

MkvWriter::~MkvWriter() noexcept
{
    try
    {
        if( file.is_open() )
            Finalize();
    }
    catch( ... )
    {
        // Destruction is only a finalization safety net and must never terminate
        // the process if allocation or an exception-enabled stream fails.
        file.exceptions( std::ios::goodbit );
        file.close();
    }
}

bool MkvWriter::IsOpen() const noexcept
{
    return file.is_open();
}

std::uint64_t MkvWriter::GetWrittenFrameCount() const noexcept
{
    return frameCount;
}

std::string_view MkvWriter::LastError() const noexcept
{
    return lastError;
}

bool MkvWriter::Reject( std::string message )
{
    if( lastError.empty() )
        lastError = std::move( message );
    return false;
}

bool MkvWriter::Fail( std::string message )
{
    if( !hasFatalError )
        lastError = std::move( message );
    hasFatalError = true;
    return false;
}

bool MkvWriter::WriteBytes( const void* const data,
                            const std::size_t size,
                            const std::string_view operation )
{
    if( size > static_cast< std::size_t >( std::numeric_limits< std::streamsize >::max() ) )
        return Fail( std::string( operation ) + ": buffer exceeds stream size limit" );

    file.write( static_cast< const char* >( data ), static_cast< std::streamsize >( size ) );
    if( !file.good() )
        return Fail( std::string( operation ) + ": output stream write failed" );
    return true;
}

bool MkvWriter::Open( std::ofstream&& outStream,
                      const std::uint32_t newWidth,
                      const std::uint32_t newHeight,
                      const std::uint32_t newFpsNum,
                      const std::uint32_t newFpsDen,
                      const std::string_view newCodecId )
{
    if( file.is_open() )
        return Reject( "Open: writer is already open" );

    lastError.clear();
    hasFatalError = false;
    if( !outStream.is_open() )
        return Reject( "Open: output stream is not open" );
    if( newWidth == 0 || newHeight == 0 )
        return Reject( "Open: dimensions must be non-zero" );
    if( newFpsNum == 0 || newFpsDen == 0 )
        return Reject( "Open: fps numerator and denominator must be non-zero" );
    if( newCodecId.empty() )
        return Reject( "Open: codec ID must not be empty" );

    width = newWidth;
    height = newHeight;
    fpsNum = newFpsNum;
    fpsDen = newFpsDen;
    codecId.assign( newCodecId );
    codecPrivate.clear();
    headersWritten = false;
    frameCount = 0;
    lastTimestampMs = 0;
    audioSampleRate = 0;
    audioChannelCount = 0;
    audioFrameCount = 0;
    lastAudioTimestampMs = 0;
    audioEndMs = 0;
    blockHeader.clear();
    clusterStartMs = 0;
    clusterOpen = false;
    cuePoints.clear();
    file = std::move( outStream );
    file.exceptions( std::ios::goodbit );

    ByteBuffer ebmlPayload;
    AppendUintElement( ebmlPayload, IdEbmlVersion, 1 );
    AppendUintElement( ebmlPayload, IdEbmlReadVersion, 1 );
    AppendUintElement( ebmlPayload, IdEbmlMaxIdLength, 4 );
    AppendUintElement( ebmlPayload, IdEbmlMaxSizeLength, 8 );
    AppendStringElement( ebmlPayload, IdDocType, "matroska" );
    AppendUintElement( ebmlPayload, IdDocTypeVersion, 4 );
    AppendUintElement( ebmlPayload, IdDocTypeReadVersion, 2 );

    ByteBuffer ebml;
    AppendMasterElement( ebml, IdEbml, ebmlPayload );
    return WriteBytes( ebml.data(), ebml.size(), "Open" );
}

bool MkvWriter::SetCodecPrivate( const std::uint8_t* const data,
                                 const std::size_t size )
{
    if( !file.is_open() )
        return Reject( "SetCodecPrivate: writer is not open" );
    if( headersWritten )
        return Reject( "SetCodecPrivate: headers are already written" );
    if( data == nullptr && size != 0 )
        return Reject( "SetCodecPrivate: data is null but size is non-zero" );

    codecPrivate.clear();
    if( size != 0 )
        codecPrivate.assign( data, data + size );
    return true;
}

bool MkvWriter::SetAudioTrack( const std::uint32_t sampleRate,
                               const std::uint32_t channelCount )
{
    if( !file.is_open() )
        return Reject( "SetAudioTrack: writer is not open" );
    if( headersWritten )
        return Reject( "SetAudioTrack: headers are already written" );
    if( sampleRate == 0 || channelCount == 0 )
        return Reject( "SetAudioTrack: sample rate and channel count must be non-zero" );

    audioSampleRate = sampleRate;
    audioChannelCount = channelCount;
    return true;
}

bool MkvWriter::WriteHeaders()
{
    ByteBuffer segment;
    AppendId( segment, IdSegment );
    if( !WriteBytes( segment.data(), segment.size(), "WriteHeaders/Segment" ) )
        return false;

    segmentSizeOffset = file.tellp();
    constexpr std::uint8_t unknownSize[ 8 ] = {
        0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
    };
    if( segmentSizeOffset == std::streampos( -1 )
        || !WriteBytes( unknownSize, sizeof( unknownSize ), "WriteHeaders/SegmentSize" ) )
        return Fail( "WriteHeaders: failed to query segment position" );
    segmentPayloadStart = file.tellp();

    seekHeadOffset = file.tellp();
    ByteBuffer voidElement;
    AppendId( voidElement, IdVoid );
    AppendSize( voidElement, SeekHeadBytes - 2 );
    voidElement.resize( SeekHeadBytes, 0 );
    if( !WriteBytes( voidElement.data(), voidElement.size(), "WriteHeaders/SeekHeadReservation" ) )
        return false;

    ByteBuffer infoPayload;
    AppendUintElement( infoPayload, IdTimestampScale, TimestampScaleNs );
    AppendStringElement( infoPayload, IdMuxingApp, "mkv-writer" );
    AppendStringElement( infoPayload, IdWritingApp, "mkv-writer" );
    const std::size_t durationOffsetInInfo = infoPayload.size();
    AppendFloat8Element( infoPayload, IdDuration, 0.0 );

    ByteBuffer info;
    AppendMasterElement( info, IdInfo, infoPayload );
    const std::size_t infoPayloadStart = info.size() - infoPayload.size();
    durationPayloadOffset = file.tellp()
        + static_cast< std::streamoff >( infoPayloadStart + durationOffsetInInfo + 3 );
    if( !WriteBytes( info.data(), info.size(), "WriteHeaders/Info" ) )
        return false;

    ByteBuffer videoPayload;
    AppendUintElement( videoPayload, IdPixelWidth, width );
    AppendUintElement( videoPayload, IdPixelHeight, height );

    ByteBuffer trackPayload;
    AppendUintElement( trackPayload, IdTrackNumber, 1 );
    AppendUintElement( trackPayload, IdTrackUid, 1 );
    AppendUintElement( trackPayload, IdTrackType, 1 );
    AppendUintElement( trackPayload, IdFlagLacing, 0 );
    AppendStringElement( trackPayload, IdCodecId, codecId );
    AppendUintElement( trackPayload,
                       IdDefaultDuration,
                       ( 1000000000ull * fpsDen + fpsNum / 2 ) / fpsNum );
    if( !codecPrivate.empty() )
        AppendBinaryElement( trackPayload, IdCodecPrivate, codecPrivate );
    AppendMasterElement( trackPayload, IdVideo, videoPayload );

    ByteBuffer trackEntries;
    AppendMasterElement( trackEntries, IdTrackEntry, trackPayload );

    if( audioSampleRate != 0 )
    {
        ByteBuffer audioPayload;
        AppendFloat8Element( audioPayload, IdSamplingFrequency, static_cast< double >( audioSampleRate ) );
        AppendUintElement( audioPayload, IdChannels, audioChannelCount );
        AppendUintElement( audioPayload, IdBitDepth, 32u );

        ByteBuffer audioTrackPayload;
        AppendUintElement( audioTrackPayload, IdTrackNumber, 2u );
        AppendUintElement( audioTrackPayload, IdTrackUid, 2u );
        AppendUintElement( audioTrackPayload, IdTrackType, 2u );
        AppendUintElement( audioTrackPayload, IdFlagLacing, 0u );
        AppendStringElement( audioTrackPayload, IdCodecId, "A_PCM/FLOAT/IEEE" );
        AppendMasterElement( audioTrackPayload, IdAudio, audioPayload );
        AppendMasterElement( trackEntries, IdTrackEntry, audioTrackPayload );
    }

    ByteBuffer tracks;
    AppendMasterElement( tracks, IdTracks, trackEntries );
    if( !WriteBytes( tracks.data(), tracks.size(), "WriteHeaders/Tracks" ) )
        return false;

    headersWritten = true;
    return true;
}

bool MkvWriter::PatchSize8( const std::streampos offset, const std::uint64_t size )
{
    if( size > MaxKnownSize8 )
        return Fail( "PatchSize8: element exceeds the 8-byte EBML size limit" );

    std::uint8_t bytes[ 8 ]{};
    bytes[ 0 ] = 0x01;
    for( std::uint32_t i = 1; i < 8; ++i )
        bytes[ i ] = static_cast< std::uint8_t >( size >> ( ( 7 - i ) * 8 ) );

    const std::streampos current = file.tellp();
    if( current == std::streampos( -1 ) )
        return Fail( "PatchSize8: failed to query stream position" );
    file.seekp( offset );
    if( !file.good() )
        return Fail( "PatchSize8: failed to seek to element size" );
    if( !WriteBytes( bytes, sizeof( bytes ), "PatchSize8" ) )
        return false;
    file.seekp( current );
    if( !file.good() )
        return Fail( "PatchSize8: failed to restore stream position" );
    return true;
}

bool MkvWriter::FlushCluster()
{
    if( !clusterOpen )
        return true;

    const std::streampos current = file.tellp();
    if( current == std::streampos( -1 ) || current < clusterSizeOffset )
        return Fail( "FlushCluster: invalid stream position" );
    const auto payloadSize = static_cast< std::uint64_t >( current - clusterSizeOffset ) - 8;
    if( !PatchSize8( clusterSizeOffset, payloadSize ) )
        return false;
    clusterOpen = false;
    return true;
}

bool MkvWriter::EnsureCluster( const std::uint64_t timestampMs, const bool cueKeyframe )
{
    if( clusterOpen )
        return true;

    const std::streampos clusterPosition = file.tellp();
    if( clusterPosition == std::streampos( -1 ) || clusterPosition < segmentPayloadStart )
        return Fail( "EnsureCluster: invalid cluster position" );
    if( cueKeyframe )
    {
        cuePoints.emplace_back(
            timestampMs,
            static_cast< std::uint64_t >( clusterPosition - segmentPayloadStart ) );
    }

    clusterStartMs = timestampMs;
    ByteBuffer header;
    AppendId( header, IdCluster );
    if( !WriteBytes( header.data(), header.size(), "EnsureCluster/Cluster" ) )
        return false;

    clusterSizeOffset = file.tellp();
    constexpr std::uint8_t unknownSize[ 8 ] = {
        0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
    };
    if( !WriteBytes( unknownSize, sizeof( unknownSize ), "EnsureCluster/ClusterSize" ) )
        return false;

    header.clear();
    AppendUintElement( header, IdClusterTimestamp, clusterStartMs );
    if( !WriteBytes( header.data(), header.size(), "EnsureCluster/ClusterTimestamp" ) )
        return false;
    clusterOpen = true;
    return true;
}

bool MkvWriter::WriteSimpleBlock( const std::uint8_t trackNumber,
                                  const std::byte* const data,
                                  const std::size_t size,
                                  const std::uint64_t timestampMs,
                                  const bool keyframe,
                                  const std::string_view operation )
{
    // Signed: a block on one track may trail a cluster opened by the other.
    const std::int64_t relative = static_cast< std::int64_t >( timestampMs )
        - static_cast< std::int64_t >( clusterStartMs );
    if( relative < std::numeric_limits< std::int16_t >::min()
        || relative > std::numeric_limits< std::int16_t >::max() )
        return Reject( std::string( operation ) + ": relative cluster timestamp exceeds int16 range" );

    const auto relativeBits = static_cast< std::uint16_t >( relative );
    blockHeader.clear();
    AppendId( blockHeader, IdSimpleBlock );
    AppendSize( blockHeader, size + 4 );
    blockHeader.push_back( static_cast< std::uint8_t >( 0x80u | trackNumber ) );
    blockHeader.push_back( static_cast< std::uint8_t >( relativeBits >> 8 ) );
    blockHeader.push_back( static_cast< std::uint8_t >( relativeBits ) );
    blockHeader.push_back( keyframe ? 0x80 : 0x00 );
    return WriteBytes( blockHeader.data(), blockHeader.size(), operation )
        && WriteBytes( data, size, operation );
}

bool MkvWriter::WriteFrame( const std::byte* const data,
                            const std::size_t size,
                            const std::uint64_t timestampMs,
                            const bool keyframe )
{
    if( !file.is_open() )
        return Reject( "WriteFrame: writer is not open" );
    if( data == nullptr || size == 0 )
        return Reject( "WriteFrame: packet must not be empty" );
    if( size > MaxKnownSize8 - 4 )
        return Reject( "WriteFrame: packet exceeds the EBML element size limit" );
    if( frameCount != 0 && timestampMs < lastTimestampMs )
        return Reject( "WriteFrame: timestamps must be monotonic" );
    if( !headersWritten && !WriteHeaders() )
        return false;

    if( clusterOpen
        && ( keyframe || timestampMs >= clusterStartMs + MaxClusterDurationMs )
        && !FlushCluster() )
        return false;

    if( !EnsureCluster( timestampMs, keyframe ) )
        return false;

    if( !WriteSimpleBlock( 1u, data, size, timestampMs, keyframe, "WriteFrame" ) )
        return false;

    ++frameCount;
    lastTimestampMs = timestampMs;
    return true;
}

bool MkvWriter::WriteAudio( const float* const samples,
                            const std::size_t sampleFrameCount,
                            const std::uint64_t timestampMs )
{
    if( !file.is_open() )
        return Reject( "WriteAudio: writer is not open" );
    if( audioSampleRate == 0 )
        return Reject( "WriteAudio: SetAudioTrack was not called" );
    if( samples == nullptr || sampleFrameCount == 0 )
        return Reject( "WriteAudio: block must not be empty" );
    const std::size_t bytesPerFrame = audioChannelCount * sizeof( float );
    if( sampleFrameCount > ( MaxKnownSize8 - 4 ) / bytesPerFrame )
        return Reject( "WriteAudio: block exceeds the EBML element size limit" );
    if( audioFrameCount != 0 && timestampMs < lastAudioTimestampMs )
        return Reject( "WriteAudio: timestamps must be monotonic" );
    if( !headersWritten && !WriteHeaders() )
        return false;

    if( clusterOpen
        && timestampMs >= clusterStartMs + MaxClusterDurationMs
        && !FlushCluster() )
        return false;

    if( !EnsureCluster( timestampMs, false ) )
        return false;

    // PCM blocks decode independently, every one is a keyframe.
    const auto* const data = reinterpret_cast< const std::byte* >( samples );
    if( !WriteSimpleBlock( 2u, data, sampleFrameCount * bytesPerFrame, timestampMs, true, "WriteAudio" ) )
        return false;

    audioFrameCount += sampleFrameCount;
    lastAudioTimestampMs = timestampMs;
    audioEndMs = timestampMs
        + static_cast< std::uint64_t >( std::llround( sampleFrameCount * 1000.0 / audioSampleRate ) );
    return true;
}

bool MkvWriter::Finalize()
{
    if( !file.is_open() )
        return !hasFatalError;

    const bool hadPriorFatalError = hasFatalError;
    bool success = true;
    if( !headersWritten )
        success = WriteHeaders();
    if( success )
        success = FlushCluster();

    if( success && !cuePoints.empty() )
    {
        const std::streampos cuesStart = file.tellp();
        if( cuesStart == std::streampos( -1 ) || cuesStart < segmentPayloadStart )
        {
            success = Fail( "Finalize: invalid cues position" );
        }
        else
        {
            const auto cuesPosition = static_cast< std::uint64_t >( cuesStart - segmentPayloadStart );
            ByteBuffer cuesPayload;
            for( const auto& [ timeMs, clusterPosition ] : cuePoints )
            {
                ByteBuffer trackPositionPayload;
                AppendUintElement( trackPositionPayload, IdCueTrack, 1 );
                AppendUintElement( trackPositionPayload, IdCueClusterPosition, clusterPosition );

                ByteBuffer pointPayload;
                AppendUintElement( pointPayload, IdCueTime, timeMs );
                AppendMasterElement( pointPayload, IdCueTrackPositions, trackPositionPayload );
                AppendMasterElement( cuesPayload, IdCuePoint, pointPayload );
            }

            ByteBuffer cues;
            AppendMasterElement( cues, IdCues, cuesPayload );
            success = WriteBytes( cues.data(), cues.size(), "Finalize/Cues" );

            if( success )
            {
                ByteBuffer seekEntry;
                AppendId( seekEntry, IdSeekId );
                AppendSize( seekEntry, 4 );
                AppendId( seekEntry, IdCues );
                AppendUintElementFixed8( seekEntry, IdSeekPosition, cuesPosition );

                ByteBuffer seekPayload;
                AppendMasterElement( seekPayload, IdSeek, seekEntry );
                ByteBuffer seekHead;
                AppendMasterElement( seekHead, IdSeekHead, seekPayload );
                if( seekHead.size() != SeekHeadBytes )
                {
                    success = Fail( "Finalize: internal SeekHead reservation mismatch" );
                }
                else
                {
                    const std::streampos cuesEnd = file.tellp();
                    file.seekp( seekHeadOffset );
                    if( !file.good() )
                    {
                        success = Fail( "Finalize: failed to seek to SeekHead reservation" );
                    }
                    else
                    {
                        success = WriteBytes(
                            seekHead.data(), seekHead.size(), "Finalize/SeekHead" );
                    }
                    if( success )
                    {
                        file.seekp( cuesEnd );
                        if( !file.good() )
                            success = Fail( "Finalize: failed to restore cues position" );
                    }
                }
            }
        }
    }

    std::streampos segmentEnd = std::streampos( -1 );
    if( success )
    {
        segmentEnd = file.tellp();
        if( segmentEnd == std::streampos( -1 ) || segmentEnd < segmentPayloadStart )
            success = Fail( "Finalize: invalid segment end position" );
    }
    if( success )
    {
        success = PatchSize8(
            segmentSizeOffset,
            static_cast< std::uint64_t >( segmentEnd - segmentPayloadStart ) );
    }

    if( success )
    {
        const double videoEndMs = frameCount == 0 ? 0.0 : static_cast< double >( lastTimestampMs ) + 1000.0 * fpsDen / fpsNum;
        const double durationMs = std::max( videoEndMs, static_cast< double >( audioEndMs ) );
        std::uint64_t bits = 0;
        std::memcpy( &bits, &durationMs, sizeof( bits ) );
        std::uint8_t durationBytes[ 8 ]{};
        for( std::uint32_t i = 0; i < 8; ++i )
            durationBytes[ i ] = static_cast< std::uint8_t >( bits >> ( ( 7 - i ) * 8 ) );

        file.seekp( durationPayloadOffset );
        if( !file.good() )
            success = Fail( "Finalize: failed to seek to Duration" );
        else
            success = WriteBytes( durationBytes, sizeof( durationBytes ), "Finalize/Duration" );
    }

    if( success )
    {
        file.flush();
        if( !file.good() )
            success = Fail( "Finalize: output stream flush failed" );
    }

    file.close();
    if( file.fail() && success )
        success = Fail( "Finalize: output stream close failed" );
    return success && !hadPriorFatalError;
}

}  // namespace mkv_writer
