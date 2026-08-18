#include <mkv_writer/MkvWriter.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>

int main()
{
    const auto output = std::filesystem::temp_directory_path()
        / "mkv-writer-cmake-consumer.mkv";
    std::filesystem::remove( output );

    mkv_writer::MkvWriter writer;
    if( !writer.Open( std::ofstream( output, std::ios::binary ), 16, 16, 30.0f, "V_AV1" ) )
        return 1;

    constexpr std::array< std::uint8_t, 3 > packet = { 0x01, 0x02, 0x03 };
    if( !writer.WriteFrame( packet.data(), packet.size(), 0, true ) )
        return 2;
    if( !writer.Finalize() )
        return 3;
    if( writer.GetWrittenFrameCount() != 1 || !std::filesystem::exists( output ) )
        return 4;

    std::filesystem::remove( output );
    return 0;
}
