#include <MkvWriter.h>
#include <algorithm>
#include <array>
#include <cstdint>
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
    RequireSuccess( writer.WriteFrame( keyframe.data(), keyframe.size(), 0, true ), writer );
    RequireSuccess( writer.WriteFrame( delta.data(), delta.size(), 17, false ), writer );
    RequireSuccess( writer.WriteFrame( keyframe.data(), keyframe.size(), 34, true ), writer );
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

void TestRejectsInvalidCallOrderAndTimestamps()
{
    const auto path = TestPath( "mkv-writer-validation-test" );
    std::filesystem::remove( path );

    mkv_writer::MkvWriter unopened;
    constexpr std::array< std::uint8_t, 1 > packet = { 0x01 };
    Require( !unopened.WriteFrame( packet.data(), packet.size(), 0, true ),
             "unopened writer accepted a packet" );

    mkv_writer::MkvWriter writer;
    RequireSuccess(
        writer.Open( std::ofstream( path, std::ios::binary ), 64, 64, 30.0f, "V_AV1" ),
        writer );
    RequireSuccess( writer.WriteFrame( packet.data(), packet.size(), 10, true ), writer );
    Require( !writer.SetCodecPrivate( packet.data(), packet.size() ),
             "late codec-private data was accepted" );
    const std::string firstError( writer.LastError() );
    Require( !writer.WriteFrame( packet.data(), packet.size(), 9, false ),
             "non-monotonic timestamp was accepted" );
    Require( writer.LastError() == firstError, "writer did not preserve the first error" );
    RequireSuccess( writer.WriteFrame( packet.data(), packet.size(), 20, false ), writer );
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
    RequireSuccess( writer.WriteFrame( packet.data(), packet.size(), 0, true ), writer );
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
    RequireSuccess( writer.WriteFrame( packet.data(), packet.size(), 0, true ), writer );
    RequireSuccess( writer.Finalize(), writer );

    RequireSuccess(
        writer.Open( std::ofstream( secondPath, std::ios::binary ), 32, 32, 24.0f, "V_AV1" ),
        writer );
    Require( writer.GetWrittenFrameCount() == 0, "Open did not reset the frame count" );
    Require( writer.LastError().empty(), "Open did not reset the prior error state" );
    RequireSuccess( writer.WriteFrame( packet.data(), packet.size(), 0, true ), writer );
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
