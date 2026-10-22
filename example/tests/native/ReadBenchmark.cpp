#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>
#include IO_FILESYSTEM_HEADER
#include IO_FILE_HANDLE_HEADER

// Optimized native-core benchmark, not an end-to-end mobile/JSI measurement.
template<typename Read>
void measure(const char* name, size_t expected, Read read) {
    std::vector<double> samples;
    for (int i = -3; i < 31; ++i) {
        const auto start = std::chrono::steady_clock::now();
        auto contents = read();
        const auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        if (contents.size() != expected || contents.front() != 'a' || contents.back() != 'z')
            throw std::runtime_error("Benchmark read returned incorrect data");
        if (i >= 0) samples.push_back(elapsed);
    }
    std::sort(samples.begin(), samples.end());
    std::cout << name << ": median=" << std::fixed << std::setprecision(3)
              << samples[samples.size() / 2] << " ms (31 samples, warm page cache)\n";
}

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    const std::string path = argv[1];
    rct_io::IOFileSystem fs;
    const auto size = static_cast<size_t>(fs.getFileSize(path));
    if (size == 0) return 1;
    measure("File.readString native core", size, [&] { return fs.readString(path); });
    rct_io::IOFileHandle handle(path, rct_io::FileOpenMode::Read);
    measure("FileHandle.readString native core", size, [&] {
        handle.seek(0);
        return handle.readString();
    });
    measure("File.readBytes native core", size, [&] { return fs.readBytes(path); });
}
