from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import collect_libs


class MkvWriterConan(ConanFile):
    name = "mkv-writer"
    version = "0.1.0"
    package_type = "static-library"

    license = "MIT"
    author = "Triada Studio"
    url = "https://github.com/triadastudio/mkv-writer.git"
    homepage = "https://github.com/triadastudio/mkv-writer"
    description = "Small Matroska writer for a single encoded video track"
    topics = ("matroska", "mkv", "video", "muxer", "av1", "hevc")

    settings = "os", "arch", "compiler", "build_type"
    options = {"fPIC": [True, False]}
    default_options = {"fPIC": True}
    exports_sources = "CMakeLists.txt", "LICENSE", "cmake/*", "src/*"

    def config_options(self):
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")

    def validate(self):
        check_min_cppstd(self, "17")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        toolchain = CMakeToolchain(self)
        toolchain.variables["MKV_WRITER_BUILD_TESTS"] = False
        toolchain.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = collect_libs(self)
        self.cpp_info.set_property("cmake_file_name", "mkv-writer")
        self.cpp_info.set_property("cmake_target_name", "mkv-writer::mkv-writer")
