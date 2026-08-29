#include <MkvWriter.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector< std::uint8_t >;
constexpr std::size_t NotFound = std::numeric_limits< std::size_t >::max();

void Require( const bool condition, const std::string_view message )
{
    if( !condition )
        throw std::runtime_error( std::string( message ) );
}

void RequireSuccess( const bool success, const mkv_writer::MkvWriter& writer )
{
    if( !success )
    {
        const auto error = writer.LastError();
        throw std::runtime_error(
            error.empty() ? "writer operation failed without a diagnostic" : std::string( error ) );
    }
}

const std::byte* AsBytes( const std::uint8_t* const data )
{
    return reinterpret_cast< const std::byte* >( data );
}

template< std::size_t Size >
std::size_t Find( const Bytes& bytes, const std::array< std::uint8_t, Size >& pattern )
{
    const auto found = std::search( bytes.begin(), bytes.end(), pattern.begin(), pattern.end() );
    return found == bytes.end() ? NotFound : static_cast< std::size_t >( found - bytes.begin() );
}

struct Element
{
    std::uint32_t id = 0;
    std::size_t payloadOffset = 0;
    std::size_t payloadSize = 0;

    [[nodiscard]] std::size_t End() const
    {
        return payloadOffset + payloadSize;
    }
};

std::size_t VintLength( const std::uint8_t firstByte )
{
    for( std::size_t length = 1; length <= 8; ++length )
    {
        if( ( firstByte & ( 0x80u >> ( length - 1 ) ) ) != 0 )
            return length;
    }
    throw std::runtime_error( "invalid EBML variable-length integer" );
}

Element ReadElement( const Bytes& bytes,
                     const std::size_t offset,
                     const std::size_t limit )
{
    Require( offset < limit && limit <= bytes.size(), "element starts outside its parent" );

    const std::size_t idLength = VintLength( bytes[ offset ] );
    Require( idLength <= 4 && idLength <= limit - offset, "invalid EBML element ID" );
    std::uint32_t id = 0;
    for( std::size_t i = 0; i < idLength; ++i )
        id = ( id << 8u ) | bytes[ offset + i ];

    const std::size_t sizeOffset = offset + idLength;
    Require( sizeOffset < limit, "missing EBML element size" );
    const std::size_t sizeLength = VintLength( bytes[ sizeOffset ] );
    Require( sizeLength <= limit - sizeOffset, "truncated EBML element size" );

    std::uint64_t payloadSize = bytes[ sizeOffset ] & ( 0xFFu >> sizeLength );
    for( std::size_t i = 1; i < sizeLength; ++i )
        payloadSize = ( payloadSize << 8u ) | bytes[ sizeOffset + i ];

    const std::size_t payloadOffset = sizeOffset + sizeLength;
    Require( payloadSize <= limit - payloadOffset, "EBML element exceeds its parent" );
    return { id, payloadOffset, static_cast< std::size_t >( payloadSize ) };
}

std::size_t CountChildren( const Bytes& bytes,
                           const Element& parent,
                           const std::uint32_t childId )
{
    std::size_t count = 0;
    std::size_t offset = parent.payloadOffset;
    while( offset < parent.End() )
    {
        const Element child = ReadElement( bytes, offset, parent.End() );
        if( child.id == childId )
            ++count;
        offset = child.End();
    }
    Require( offset == parent.End(), "EBML children do not fill their parent" );
    return count;
}

Bytes ReadAll( const std::filesystem::path& path )
{
    std::ifstream input( path, std::ios::binary );
    Require( input.is_open(), "failed to reopen test output" );
    return Bytes( std::istreambuf_iterator< char >( input ), {} );
}

std::filesystem::path TestPath( const std::string_view name )
{
    return std::filesystem::temp_directory_path() / ( std::string( name ) + ".mkv" );
}

void TestWritesAndFinalizesMatroska()
{
    const auto path = TestPath( "mkv-writer-structure-test" );
    std::filesystem::remove( path );

    mkv_writer::MkvWriter writer;
    RequireSuccess(
        writer.Open( std::ofstream( path, std::ios::binary ), 1920, 1080, 60.0f, "V_AV1" ),
        writer );
    constexpr std::array< std::uint8_t, 4 > codecPrivate = { 0x81, 0x00, 0x0C, 0x00 };
    RequireSuccess( writer.SetCodecPrivate( codecPrivate.data(), codecPrivate.size() ), writer );

    constexpr std::array< std::uint8_t, 5 > keyframe = { 0xAA, 0x10, 0x20, 0x30, 0xBB };
    constexpr std::array< std::uint8_t, 4 > delta = { 0xCC, 0x40, 0x50, 0xDD };
    RequireSuccess( writer.WriteFrame( AsBytes( keyframe.data() ), keyframe.size(), 0, true ), writer );
    RequireSuccess( writer.WriteFrame( AsBytes( delta.data() ), delta.size(), 17, false ), writer );
    RequireSuccess( writer.WriteFrame( AsBytes( keyframe.data() ), keyframe.size(), 34, true ), writer );
    Require( writer.GetWrittenFrameCount() == 3, "unexpected frame count" );
    RequireSuccess( writer.Finalize(), writer );
    Require( !writer.IsOpen(), "Finalize did not close output" );
    Require( writer.Finalize(), "Finalize is not idempotent" );

    const Bytes bytes = ReadAll( path );
    constexpr std::array ebml = { std::uint8_t{ 0x1A }, std::uint8_t{ 0x45 }, std::uint8_t{ 0xDF }, std::uint8_t{ 0xA3 } };
    constexpr std::array segment = { std::uint8_t{ 0x18 }, std::uint8_t{ 0x53 }, std::uint8_t{ 0x80 }, std::uint8_t{ 0x67 } };
    constexpr std::array tracks = { std::uint8_t{ 0x16 }, std::uint8_t{ 0x54 }, std::uint8_t{ 0xAE }, std::uint8_t{ 0x6B } };
    constexpr std::array cluster = { std::uint8_t{ 0x1F }, std::uint8_t{ 0x43 }, std::uint8_t{ 0xB6 }, std::uint8_t{ 0x75 } };
    constexpr std::array cues = { std::uint8_t{ 0x1C }, std::uint8_t{ 0x53 }, std::uint8_t{ 0xBB }, std::uint8_t{ 0x6B } };
    constexpr std::array seekHead = { std::uint8_t{ 0x11 }, std::uint8_t{ 0x4D }, std::uint8_t{ 0x9B }, std::uint8_t{ 0x74 } };

    Require( Find( bytes, ebml ) == 0, "missing EBML header" );
    const auto segmentOffset = Find( bytes, segment );
    Require( segmentOffset != NotFound, "missing Segment" );
    Require( Find( bytes, tracks ) != NotFound, "missing Tracks" );
    const auto clusterOffset = Find( bytes, cluster );
    Require( clusterOffset != NotFound, "missing Cluster" );
    Require( Find( bytes, cues ) != NotFound, "missing Cues" );
    Require( Find( bytes, seekHead ) != NotFound, "missing SeekHead" );
    Require( Find( bytes, codecPrivate ) != NotFound, "missing codec-private bytes" );
    Require( Find( bytes, keyframe ) != NotFound, "missing packet bytes" );

    const auto isUnknownSize = [ &bytes ]( const std::size_t offset ) {
        constexpr std::array unknown = { std::uint8_t{ 0x01 }, std::uint8_t{ 0xFF }, std::uint8_t{ 0xFF }, std::uint8_t{ 0xFF }, std::uint8_t{ 0xFF }, std::uint8_t{ 0xFF }, std::uint8_t{ 0xFF }, std::uint8_t{ 0xFF } };
        return std::equal( unknown.begin(), unknown.end(), bytes.begin() + offset );
    };
    Require( !isUnknownSize( segmentOffset + segment.size() ), "Segment size was not patched" );
    Require( !isUnknownSize( clusterOffset + cluster.size() ), "Cluster size was not patched" );

    constexpr std::uint32_t IdCluster = 0x1F43B675u;
    constexpr std::uint32_t IdCues = 0x1C53BB6Bu;
    constexpr std::uint32_t IdCuePoint = 0xBBu;
    const Element segmentElement = ReadElement( bytes, segmentOffset, bytes.size() );
    Require( CountChildren( bytes, segmentElement, IdCluster ) == 2,
             "expected one cluster per keyframe" );

    std::size_t cuePointCount = 0;
    std::size_t childOffset = segmentElement.payloadOffset;
    while( childOffset < segmentElement.End() )
    {
        const Element child = ReadElement( bytes, childOffset, segmentElement.End() );
        if( child.id == IdCues )
            cuePointCount += CountChildren( bytes, child, IdCuePoint );
        childOffset = child.End();
    }
    Require( cuePointCount == 2, "expected one cue point per keyframe" );

    std::filesystem::remove( path );
}

void TestWritesInterleavedAudioTrack()
{
    const auto path = TestPath( "mkv-writer-audio-track-test" );
    std::filesystem::remove( path );

    mkv_writer::MkvWriter writer;
    RequireSuccess(
        writer.Open( std::ofstream( path, std::ios::binary ), 320, 180, 60.0f, "V_AV1" ),
        writer );
    RequireSuccess( writer.SetAudioTrack( 1000, 2 ), writer );

    constexpr std::array< std::uint8_t, 5 > keyframe = { 0xAA, 0x10, 0x20, 0x30, 0xBB };
    constexpr std::array< std::uint8_t, 4 > delta = { 0xCC, 0x40, 0x50, 0xDD };
    constexpr std::array< float, 8 > block = {
        0.5f, -1.0f, 0.25f, -0.125f, 0.75f, -0.375f, 1.0f, -0.5f
    };
    const std::vector< float > tail( 200, 0.125f );

    RequireSuccess( writer.WriteFrame( AsBytes( keyframe.data() ), keyframe.size(), 0, true ), writer );
    RequireSuccess( writer.WriteAudio( block.data(), block.size() / 2, 0 ), writer );
    RequireSuccess( writer.WriteFrame( AsBytes( delta.data() ), delta.size(), 17, false ), writer );
    RequireSuccess( writer.WriteAudio( block.data(), block.size() / 2, 17 ), writer );
    RequireSuccess( writer.WriteFrame( AsBytes( keyframe.data() ), keyframe.size(), 34, true ), writer );
    // Trails the cluster the video keyframe just opened: negative relative timestamp.
    RequireSuccess( writer.WriteAudio( tail.data(), tail.size() / 2, 30 ), writer );
    RequireSuccess( writer.Finalize(), writer );

    const Bytes bytes = ReadAll( path );
    constexpr std::array segment = { std::uint8_t{ 0x18 }, std::uint8_t{ 0x53 }, std::uint8_t{ 0x80 }, std::uint8_t{ 0x67 } };
    constexpr std::array pcmCodec = {
        std::uint8_t{ 'A' }, std::uint8_t{ '_' }, std::uint8_t{ 'P' }, std::uint8_t{ 'C' }, std::uint8_t{ 'M' }, std::uint8_t{ '/' }, std::uint8_t{ 'F' }, std::uint8_t{ 'L' }, std::uint8_t{ 'O' }, std::uint8_t{ 'A' }, std::uint8_t{ 'T' }, std::uint8_t{ '/' }, std::uint8_t{ 'I' }, std::uint8_t{ 'E' }, std::uint8_t{ 'E' }, std::uint8_t{ 'E' }
    };
    Require( Find( bytes, pcmCodec ) != NotFound, "missing PCM codec ID" );

    std::array< std::uint8_t, sizeof( block ) > blockBytes{};
    std::memcpy( blockBytes.data(), block.data(), sizeof( block ) );
    Require( Find( bytes, blockBytes ) != NotFound, "missing audio sample bytes" );

    constexpr std::uint32_t IdTracks = 0x1654AE6Bu;
    constexpr std::uint32_t IdTrackEntry = 0xAEu;
    constexpr std::uint32_t IdInfo = 0x1549A966u;
    constexpr std::uint32_t IdDuration = 0x4489u;
    constexpr std::uint32_t IdCluster = 0x1F43B675u;
    constexpr std::uint32_t IdSimpleBlock = 0xA3u;
    constexpr std::uint32_t IdCues = 0x1C53BB6Bu;
    constexpr std::uint32_t IdCuePoint = 0xBBu;

    const auto segmentOffset = Find( bytes, segment );
    Require( segmentOffset != NotFound, "missing Segment" );
    const Element segmentElement = ReadElement( bytes, segmentOffset, bytes.size() );

    std::size_t trackEntryCount = 0;
    std::size_t simpleBlockCount = 0;
    std::size_t clusterCount = 0;
    Element lastCluster;
    std::size_t cuePointCount = 0;
    double durationMs = 0.0;
    std::size_t childOffset = segmentElement.payloadOffset;
    while( childOffset < segmentElement.End() )
    {
        const Element child = ReadElement( bytes, childOffset, segmentElement.End() );
        if( child.id == IdTracks )
            trackEntryCount = CountChildren( bytes, child, IdTrackEntry );
        if( child.id == IdCluster )
        {
            ++clusterCount;
            lastCluster = child;
            simpleBlockCount += CountChildren( bytes, child, IdSimpleBlock );
        }
        if( child.id == IdCues )
            cuePointCount = CountChildren( bytes, child, IdCuePoint );
        if( child.id == IdInfo )
        {
            std::size_t infoOffset = child.payloadOffset;
            while( infoOffset < child.End() )
            {
                const Element infoChild = ReadElement( bytes, infoOffset, child.End() );
                if( infoChild.id == IdDuration )
                {
                    Require( infoChild.payloadSize == 8, "unexpected Duration size" );
                    std::uint64_t bits = 0;
                    for( std::size_t i = 0; i < 8; ++i )
                        bits = ( bits << 8u ) | bytes[ infoChild.payloadOffset + i ];
                    std::memcpy( &durationMs, &bits, sizeof( durationMs ) );
                }
                infoOffset = infoChild.End();
            }
        }
        childOffset = child.End();
    }

    Require( trackEntryCount == 2, "expected a video and an audio track entry" );
    Require( simpleBlockCount == 6, "expected three video and three audio blocks" );
    Require( clusterCount == 2, "the trailing audio block must not rotate the cluster" );
    Require( cuePointCount == 2, "audio blocks must not add cue points" );

    // Audio at 30 rides the cluster the keyframe opened at 34: relative -4.
    bool trailingAudioFound = false;
    std::size_t blockOffset = lastCluster.payloadOffset;
    while( blockOffset < lastCluster.End() )
    {
        const Element simpleBlock = ReadElement( bytes, blockOffset, lastCluster.End() );
        if( simpleBlock.id == IdSimpleBlock && bytes[ simpleBlock.payloadOffset ] == 0x82u )
        {
            const auto relativeBits = static_cast< std::uint16_t >(
                ( bytes[ simpleBlock.payloadOffset + 1 ] << 8u ) | bytes[ simpleBlock.payloadOffset + 2 ] );
            trailingAudioFound = static_cast< std::int16_t >( relativeBits ) == -4;
        }
        blockOffset = simpleBlock.End();
    }
    Require( trailingAudioFound, "the trailing audio block lost its negative relative timestamp" );
    // Last audio block: 100 frames at 1 kHz from 30 ms, past the video end.
    Require( durationMs == 130.0, "duration does not cover the audio tail" );

    std::filesystem::remove( path );
}

void TestRejectsAudioMisuse()
{
    const auto path = TestPath( "mkv-writer-audio-misuse-test" );
    const auto secondPath = TestPath( "mkv-writer-audio-misuse-second-test" );
    std::filesystem::remove( path );
    std::filesystem::remove( secondPath );

    constexpr std::array< float, 4 > block = { 0.1f, -0.1f, 0.2f, -0.2f };

    mkv_writer::MkvWriter writer;
    RequireSuccess(
        writer.Open( std::ofstream( path, std::ios::binary ), 64, 64, 30.0f, "V_AV1" ),
        writer );
    Require( !writer.SetAudioTrack( 0, 2 ), "zero sample rate was accepted" );
    Require( !writer.SetAudioTrack( 48000, 0 ), "zero channel count was accepted" );
    Require( !writer.WriteAudio( block.data(), block.size() / 2, 0 ),
             "audio without SetAudioTrack was accepted" );

    RequireSuccess( writer.SetAudioTrack( 48000, 2 ), writer );
    RequireSuccess( writer.WriteAudio( block.data(), block.size() / 2, 100 ), writer );
    Require( !writer.SetAudioTrack( 44100, 2 ), "late SetAudioTrack was accepted" );
    Require( !writer.WriteAudio( nullptr, 2, 200 ), "null audio block was accepted" );
    Require( !writer.WriteAudio( block.data(), block.size() / 2, 50 ),
             "non-monotonic audio timestamp was accepted" );
    RequireSuccess( writer.Finalize(), writer );

    RequireSuccess(
        writer.Open( std::ofstream( secondPath, std::ios::binary ), 64, 64, 30.0f, "V_AV1" ),
        writer );
    Require( !writer.WriteAudio( block.data(), block.size() / 2, 0 ),
             "Open did not reset the audio track" );
    RequireSuccess( writer.Finalize(), writer );

    std::filesystem::remove( path );
    std::filesystem::remove( secondPath );
}

void TestRejectsInvalidCallOrderAndTimestamps()
{
    const auto path = TestPath( "mkv-writer-validation-test" );
    std::filesystem::remove( path );

    mkv_writer::MkvWriter unopened;
    constexpr std::array< std::uint8_t, 1 > packet = { 0x01 };
    Require( !unopened.WriteFrame( AsBytes( packet.data() ), packet.size(), 0, true ),
             "unopened writer accepted a packet" );

    mkv_writer::MkvWriter writer;
    RequireSuccess(
        writer.Open( std::ofstream( path, std::ios::binary ), 64, 64, 30.0f, "V_AV1" ),
        writer );
    RequireSuccess( writer.WriteFrame( AsBytes( packet.data() ), packet.size(), 10, true ), writer );
    Require( !writer.SetCodecPrivate( packet.data(), packet.size() ),
             "late codec-private data was accepted" );
    const std::string firstError( writer.LastError() );
    Require( !writer.WriteFrame( AsBytes( packet.data() ), packet.size(), 9, false ),
             "non-monotonic timestamp was accepted" );
    Require( writer.LastError() == firstError, "writer did not preserve the first error" );
    RequireSuccess( writer.WriteFrame( AsBytes( packet.data() ), packet.size(), 20, false ), writer );
    RequireSuccess( writer.Finalize(), writer );
    Require( !writer.LastError().empty(), "validation failure has no diagnostic" );

    std::filesystem::remove( path );
}

void TestClearsCodecPrivate()
{
    const auto path = TestPath( "mkv-writer-clear-codec-private-test" );
    std::filesystem::remove( path );

    mkv_writer::MkvWriter writer;
    RequireSuccess(
        writer.Open( std::ofstream( path, std::ios::binary ), 64, 64, 30.0f, "V_AV1" ),
        writer );
    constexpr std::array< std::uint8_t, 8 > codecPrivate = {
        0xDE, 0xAD, 0xFA, 0xCE, 0xC0, 0xFF, 0xEE, 0x42
    };
    RequireSuccess( writer.SetCodecPrivate( codecPrivate.data(), codecPrivate.size() ), writer );
    RequireSuccess( writer.SetCodecPrivate( nullptr, 0 ), writer );

    constexpr std::array< std::uint8_t, 1 > packet = { 0x01 };
    RequireSuccess( writer.WriteFrame( AsBytes( packet.data() ), packet.size(), 0, true ), writer );
    RequireSuccess( writer.Finalize(), writer );
    Require( Find( ReadAll( path ), codecPrivate ) == NotFound,
             "cleared codec-private bytes were written" );
    std::filesystem::remove( path );
}

void TestReusesWriterAfterFinalize()
{
    const auto firstPath = TestPath( "mkv-writer-reuse-first-test" );
    const auto secondPath = TestPath( "mkv-writer-reuse-second-test" );
    std::filesystem::remove( firstPath );
    std::filesystem::remove( secondPath );

    mkv_writer::MkvWriter writer;
    constexpr std::array< std::uint8_t, 1 > packet = { 0x01 };
    RequireSuccess(
        writer.Open( std::ofstream( firstPath, std::ios::binary ), 64, 64, 30.0f, "V_AV1" ),
        writer );
    RequireSuccess( writer.WriteFrame( AsBytes( packet.data() ), packet.size(), 0, true ), writer );
    RequireSuccess( writer.Finalize(), writer );

    RequireSuccess(
        writer.Open( std::ofstream( secondPath, std::ios::binary ), 32, 32, 24.0f, "V_AV1" ),
        writer );
    Require( writer.GetWrittenFrameCount() == 0, "Open did not reset the frame count" );
    Require( writer.LastError().empty(), "Open did not reset the prior error state" );
    RequireSuccess( writer.WriteFrame( AsBytes( packet.data() ), packet.size(), 0, true ), writer );
    RequireSuccess( writer.Finalize(), writer );
    Require( writer.GetWrittenFrameCount() == 1, "reused writer has an invalid frame count" );

    std::filesystem::remove( firstPath );
    std::filesystem::remove( secondPath );
}

void TestDestructorFinalizesEmptyFile()
{
    const auto path = TestPath( "mkv-writer-destructor-test" );
    std::filesystem::remove( path );
    {
        mkv_writer::MkvWriter writer;
        RequireSuccess(
            writer.Open( std::ofstream( path, std::ios::binary ), 320, 180, 24.0f, "V_AV1" ),
            writer );
    }

    const Bytes bytes = ReadAll( path );
    constexpr std::array segment = { std::uint8_t{ 0x18 }, std::uint8_t{ 0x53 }, std::uint8_t{ 0x80 }, std::uint8_t{ 0x67 } };
    constexpr std::array tracks = { std::uint8_t{ 0x16 }, std::uint8_t{ 0x54 }, std::uint8_t{ 0xAE }, std::uint8_t{ 0x6B } };
    Require( Find( bytes, segment ) != NotFound, "destructor did not write Segment" );
    Require( Find( bytes, tracks ) != NotFound, "destructor did not write Tracks" );
    std::filesystem::remove( path );
}

}  // namespace

int main()
{
    try
    {
        TestWritesAndFinalizesMatroska();
        TestWritesInterleavedAudioTrack();
        TestRejectsAudioMisuse();
        TestRejectsInvalidCallOrderAndTimestamps();
        TestClearsCodecPrivate();
        TestReusesWriterAfterFinalize();
        TestDestructorFinalizesEmptyFile();
        std::cout << "All mkv-writer tests passed\n";
        return 0;
    }
    catch( const std::exception& error )
    {
        std::cerr << "mkv-writer test failure: " << error.what() << '\n';
        return 1;
    }
}
