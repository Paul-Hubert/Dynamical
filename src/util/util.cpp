#include "util.h"

#include <iostream>
#include <fstream>

using namespace dy;

std::vector<char> dy::readFile(const char* filename) {
	
	std::ifstream file(filename, std::ios::binary | std::ios::ate);

	if (!file.good()) {
		std::cout << "File with name : " << filename << " could not be opened" << std::endl;
	}

	std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);

	std::vector<char> buffer(size);
	if (file.read(buffer.data(), size)) {

		return buffer;

	}

	std::cout << "File with name : " << filename << " could not be read" << std::endl;
    return {};
}
