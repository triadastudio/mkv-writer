#include "MkvWriter.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace mkv_writer {

namespace {

using ByteBuffer = std::vector< std::uint8_t >;

constexpr std::uint32_t kIdEbml = 0x1A45DFA3u;
constexpr std::uint32_t kIdEbmlVersion = 0x4286u;
constexpr std::uint32_t kIdEbmlReadVersion = 0x42F7u;
constexpr std::uint32_t kIdEbmlMaxIdLength = 0x42F2u;
constexpr std::uint32_t kIdEbmlMaxSizeLength = 0x42F3u;
constexpr std::uint32_t kIdDocType = 0x4282u;
constexpr std::uint32_t kIdDocTypeVersion = 0x4287u;
constexpr std::uint32_t kIdDocTypeReadVersion = 0x4285u;
constexpr std::uint32_t kIdSegment = 0x18538067u;
constexpr std::uint32_t kIdInfo = 0x1549A966u;
constexpr std::uint32_t kIdTimestampScale = 0x2AD7B1u;
constexpr std::uint32_t kIdMuxingApp = 0x4D80u;
constexpr std::uint32_t kIdWritingApp = 0x5741u;
constexpr std::uint32_t kIdDuration = 0x4489u;
constexpr std::uint32_t kIdTracks = 0x1654AE6Bu;
constexpr std::uint32_t kIdTrackEntry = 0xAEu;
constexpr std::uint32_t kIdTrackNumber = 0xD7u;
constexpr std::uint32_t kIdTrackUid = 0x73C5u;
constexpr std::uint32_t kIdTrackType = 0x83u;
constexpr std::uint32_t kIdFlagLacing = 0x9Cu;
constexpr std::uint32_t kIdCodecId = 0x86u;
constexpr std::uint32_t kIdCodecPrivate = 0x63A2u;
constexpr std::uint32_t kIdDefaultDuration = 0x23E383u;
constexpr std::uint32_t kIdVideo = 0xE0u;
constexpr std::uint32_t kIdPixelWidth = 0xB0u;
constexpr std::uint32_t kIdPixelHeight = 0xBAu;
constexpr std::uint32_t kIdCluster = 0x1F43B675u;
constexpr std::uint32_t kIdClusterTimestamp = 0xE7u;
constexpr std::uint32_t kIdSimpleBlock = 0xA3u;
constexpr std::uint32_t kIdSeekHead = 0x114D9B74u;
constexpr std::uint32_t kIdSeek = 0x4DBBu;
constexpr std::uint32_t kIdSeekId = 0x53ABu;
constexpr std::uint32_t kIdSeekPosition = 0x53ACu;
constexpr std::uint32_t kIdVoid = 0xECu;
constexpr std::uint32_t kIdCues = 0x1C53BB6Bu;
constexpr std::uint32_t kIdCuePoint = 0xBBu;
constexpr std::uint32_t kIdCueTime = 0xB3u;
constexpr std::uint32_t kIdCueTrackPositions = 0xB7u;
constexpr std::uint32_t kIdCueTrack = 0xF7u;
constexpr std::uint32_t kIdCueClusterPosition = 0xF1u;

constexpr std::size_t kSeekHeadBytes = 26;
constexpr std::uint64_t kTimestampScaleNs = 1'000'000;
constexpr std::uint64_t kMaxClusterDurationMs = 5'000;
constexpr std::uint64_t kMaxKnownSize8 = ( 1ull << 56u ) - 2u;

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
                      const float newFps,
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
    if( !std::isfinite( newFps ) || newFps <= 0.0f )
        return Reject( "Open: fps must be finite and positive" );
    if( newCodecId.empty() )
        return Reject( "Open: codec ID must not be empty" );

    width = newWidth;
    height = newHeight;
    fps = newFps;
    codecId.assign( newCodecId );
    codecPrivate.clear();
    headersWritten = false;
    frameCount = 0;
    lastTimestampMs = 0;
    blockHeader.clear();
    clusterStartMs = 0;
    clusterOpen = false;
    cuePoints.clear();
    file = std::move( outStream );
    file.exceptions( std::ios::goodbit );

    ByteBuffer ebmlPayload;
    AppendUintElement( ebmlPayload, kIdEbmlVersion, 1 );
    AppendUintElement( ebmlPayload, kIdEbmlReadVersion, 1 );
    AppendUintElement( ebmlPayload, kIdEbmlMaxIdLength, 4 );
    AppendUintElement( ebmlPayload, kIdEbmlMaxSizeLength, 8 );
    AppendStringElement( ebmlPayload, kIdDocType, "matroska" );
    AppendUintElement( ebmlPayload, kIdDocTypeVersion, 4 );
    AppendUintElement( ebmlPayload, kIdDocTypeReadVersion, 2 );

    ByteBuffer ebml;
    AppendMasterElement( ebml, kIdEbml, ebmlPayload );
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

bool MkvWriter::WriteHeaders()
{
    ByteBuffer segment;
    AppendId( segment, kIdSegment );
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
    AppendId( voidElement, kIdVoid );
    AppendSize( voidElement, kSeekHeadBytes - 2 );
    voidElement.resize( kSeekHeadBytes, 0 );
    if( !WriteBytes( voidElement.data(), voidElement.size(), "WriteHeaders/SeekHeadReservation" ) )
        return false;

    ByteBuffer infoPayload;
    AppendUintElement( infoPayload, kIdTimestampScale, kTimestampScaleNs );
    AppendStringElement( infoPayload, kIdMuxingApp, "mkv-writer" );
    AppendStringElement( infoPayload, kIdWritingApp, "mkv-writer" );
    const std::size_t durationOffsetInInfo = infoPayload.size();
    AppendFloat8Element( infoPayload, kIdDuration, 0.0 );

    ByteBuffer info;
    AppendMasterElement( info, kIdInfo, infoPayload );
    const std::size_t infoPayloadStart = info.size() - infoPayload.size();
    durationPayloadOffset = file.tellp()
        + static_cast< std::streamoff >( infoPayloadStart + durationOffsetInInfo + 3 );
    if( !WriteBytes( info.data(), info.size(), "WriteHeaders/Info" ) )
        return false;

    ByteBuffer videoPayload;
    AppendUintElement( videoPayload, kIdPixelWidth, width );
    AppendUintElement( videoPayload, kIdPixelHeight, height );

    ByteBuffer trackPayload;
    AppendUintElement( trackPayload, kIdTrackNumber, 1 );
    AppendUintElement( trackPayload, kIdTrackUid, 1 );
    AppendUintElement( trackPayload, kIdTrackType, 1 );
    AppendUintElement( trackPayload, kIdFlagLacing, 0 );
    AppendStringElement( trackPayload, kIdCodecId, codecId );
    AppendUintElement( trackPayload,
                       kIdDefaultDuration,
                       static_cast< std::uint64_t >( std::llround( 1e9 / fps ) ) );
    if( !codecPrivate.empty() )
        AppendBinaryElement( trackPayload, kIdCodecPrivate, codecPrivate );
    AppendMasterElement( trackPayload, kIdVideo, videoPayload );

    ByteBuffer trackEntry;
    AppendMasterElement( trackEntry, kIdTrackEntry, trackPayload );
    ByteBuffer tracks;
    AppendMasterElement( tracks, kIdTracks, trackEntry );
    if( !WriteBytes( tracks.data(), tracks.size(), "WriteHeaders/Tracks" ) )
        return false;

    headersWritten = true;
    return true;
}

bool MkvWriter::PatchSize8( const std::streampos offset, const std::uint64_t size )
{
    if( size > kMaxKnownSize8 )
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

bool MkvWriter::WriteFrame( const void* const data,
                            const std::size_t size,
                            const std::uint64_t timestampMs,
                            const bool keyframe )
{
    if( !file.is_open() )
        return Reject( "WriteFrame: writer is not open" );
    if( data == nullptr || size == 0 )
        return Reject( "WriteFrame: packet must not be empty" );
    if( size > kMaxKnownSize8 - 4 )
        return Reject( "WriteFrame: packet exceeds the EBML element size limit" );
    if( frameCount != 0 && timestampMs < lastTimestampMs )
        return Reject( "WriteFrame: timestamps must be monotonic" );
    if( !headersWritten && !WriteHeaders() )
        return false;

    if( clusterOpen
        && ( keyframe || timestampMs - clusterStartMs >= kMaxClusterDurationMs )
        && !FlushCluster() )
        return false;

    if( !clusterOpen )
    {
        const std::streampos clusterPosition = file.tellp();
        if( clusterPosition == std::streampos( -1 ) || clusterPosition < segmentPayloadStart )
            return Fail( "WriteFrame: invalid cluster position" );
        if( keyframe )
        {
            cuePoints.emplace_back(
                timestampMs,
                static_cast< std::uint64_t >( clusterPosition - segmentPayloadStart ) );
        }

        clusterStartMs = timestampMs;
        ByteBuffer header;
        AppendId( header, kIdCluster );
        if( !WriteBytes( header.data(), header.size(), "WriteFrame/Cluster" ) )
            return false;

        clusterSizeOffset = file.tellp();
        constexpr std::uint8_t unknownSize[ 8 ] = {
            0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
        };
        if( !WriteBytes( unknownSize, sizeof( unknownSize ), "WriteFrame/ClusterSize" ) )
            return false;

        header.clear();
        AppendUintElement( header, kIdClusterTimestamp, clusterStartMs );
        if( !WriteBytes( header.data(), header.size(), "WriteFrame/ClusterTimestamp" ) )
            return false;
        clusterOpen = true;
    }

    const std::uint64_t relative = timestampMs - clusterStartMs;
    if( relative > static_cast< std::uint64_t >( std::numeric_limits< std::int16_t >::max() ) )
        return Reject( "WriteFrame: relative cluster timestamp exceeds int16 range" );

    blockHeader.clear();
    AppendId( blockHeader, kIdSimpleBlock );
    AppendSize( blockHeader, size + 4 );
    blockHeader.push_back( 0x81 );
    blockHeader.push_back( static_cast< std::uint8_t >( relative >> 8 ) );
    blockHeader.push_back( static_cast< std::uint8_t >( relative ) );
    blockHeader.push_back( keyframe ? 0x80 : 0x00 );
    if( !WriteBytes( blockHeader.data(), blockHeader.size(), "WriteFrame/BlockHeader" )
        || !WriteBytes( data, size, "WriteFrame/Packet" ) )
        return false;

    ++frameCount;
    lastTimestampMs = timestampMs;
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
                AppendUintElement( trackPositionPayload, kIdCueTrack, 1 );
                AppendUintElement( trackPositionPayload, kIdCueClusterPosition, clusterPosition );

                ByteBuffer pointPayload;
                AppendUintElement( pointPayload, kIdCueTime, timeMs );
                AppendMasterElement( pointPayload, kIdCueTrackPositions, trackPositionPayload );
                AppendMasterElement( cuesPayload, kIdCuePoint, pointPayload );
            }

            ByteBuffer cues;
            AppendMasterElement( cues, kIdCues, cuesPayload );
            success = WriteBytes( cues.data(), cues.size(), "Finalize/Cues" );

            if( success )
            {
                ByteBuffer seekEntry;
                AppendId( seekEntry, kIdSeekId );
                AppendSize( seekEntry, 4 );
                AppendId( seekEntry, kIdCues );
                AppendUintElementFixed8( seekEntry, kIdSeekPosition, cuesPosition );

                ByteBuffer seekPayload;
                AppendMasterElement( seekPayload, kIdSeek, seekEntry );
                ByteBuffer seekHead;
                AppendMasterElement( seekHead, kIdSeekHead, seekPayload );
                if( seekHead.size() != kSeekHeadBytes )
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
        const double durationMs = frameCount == 0 ? 0.0 : static_cast< double >( lastTimestampMs ) + 1000.0 / fps;
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
