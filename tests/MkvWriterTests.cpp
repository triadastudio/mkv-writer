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
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector< std::uint8_t >;
constexpr std::size_t kNotFound = std::numeric_limits< std::size_t >::max();

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
    return found == bytes.end() ? kNotFound : static_cast< std::size_t >( found - bytes.begin() );
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
    constexpr std::array ebml = { std::uint8_t{ 0x1A }, std::uint8_t{ 0x45 },
                                  std::uint8_t{ 0xDF }, std::uint8_t{ 0xA3 } };
    constexpr std::array segment = { std::uint8_t{ 0x18 }, std::uint8_t{ 0x53 },
                                     std::uint8_t{ 0x80 }, std::uint8_t{ 0x67 } };
    constexpr std::array tracks = { std::uint8_t{ 0x16 }, std::uint8_t{ 0x54 },
                                    std::uint8_t{ 0xAE }, std::uint8_t{ 0x6B } };
    constexpr std::array cluster = { std::uint8_t{ 0x1F }, std::uint8_t{ 0x43 },
                                     std::uint8_t{ 0xB6 }, std::uint8_t{ 0x75 } };
    constexpr std::array cues = { std::uint8_t{ 0x1C }, std::uint8_t{ 0x53 },
                                  std::uint8_t{ 0xBB }, std::uint8_t{ 0x6B } };
    constexpr std::array seekHead = { std::uint8_t{ 0x11 }, std::uint8_t{ 0x4D },
                                      std::uint8_t{ 0x9B }, std::uint8_t{ 0x74 } };

    Require( Find( bytes, ebml ) == 0, "missing EBML header" );
    const auto segmentOffset = Find( bytes, segment );
    Require( segmentOffset != kNotFound, "missing Segment" );
    Require( Find( bytes, tracks ) != kNotFound, "missing Tracks" );
    const auto clusterOffset = Find( bytes, cluster );
    Require( clusterOffset != kNotFound, "missing Cluster" );
    Require( Find( bytes, cues ) != kNotFound, "missing Cues" );
    Require( Find( bytes, seekHead ) != kNotFound, "missing SeekHead" );
    Require( Find( bytes, codecPrivate ) != kNotFound, "missing codec-private bytes" );
    Require( Find( bytes, keyframe ) != kNotFound, "missing packet bytes" );

    const auto isUnknownSize = [&bytes]( const std::size_t offset ) {
        constexpr std::array unknown = { std::uint8_t{ 0x01 }, std::uint8_t{ 0xFF },
                                         std::uint8_t{ 0xFF }, std::uint8_t{ 0xFF },
                                         std::uint8_t{ 0xFF }, std::uint8_t{ 0xFF },
                                         std::uint8_t{ 0xFF }, std::uint8_t{ 0xFF } };
        return std::equal( unknown.begin(), unknown.end(), bytes.begin() + offset );
    };
    Require( !isUnknownSize( segmentOffset + segment.size() ), "Segment size was not patched" );
    Require( !isUnknownSize( clusterOffset + cluster.size() ), "Cluster size was not patched" );

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
    Require( !writer.Finalize(), "Finalize hid an earlier error" );
    Require( !writer.LastError().empty(), "validation failure has no diagnostic" );

    std::filesystem::remove( path );
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
    constexpr std::array segment = { std::uint8_t{ 0x18 }, std::uint8_t{ 0x53 },
                                     std::uint8_t{ 0x80 }, std::uint8_t{ 0x67 } };
    constexpr std::array tracks = { std::uint8_t{ 0x16 }, std::uint8_t{ 0x54 },
                                    std::uint8_t{ 0xAE }, std::uint8_t{ 0x6B } };
    Require( Find( bytes, segment ) != kNotFound, "destructor did not write Segment" );
    Require( Find( bytes, tracks ) != kNotFound, "destructor did not write Tracks" );
    std::filesystem::remove( path );
}

}  // namespace

int main()
{
    try
    {
        TestWritesAndFinalizesMatroska();
        TestRejectsInvalidCallOrderAndTimestamps();
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
