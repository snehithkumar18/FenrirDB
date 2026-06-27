#ifdef STANDALONE_FUZZER
#include <iostream>
#include <fstream>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input_file>" << std::endl;
        return 1;
    }

    std::ifstream fs(argv[1], std::ios::binary);
    if (!fs.is_open()) {
        std::cerr << "Failed to open: " << argv[1] << std::endl;
        return 1;
    }

    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(fs)), std::istreambuf_iterator<char>());
    std::cout << "Running standalone fuzzer with " << buffer.size() << " bytes." << std::endl;
    
    LLVMFuzzerTestOneInput(buffer.data(), buffer.size());
    
    std::cout << "Execution finished." << std::endl;
    return 0;
}
#endif
