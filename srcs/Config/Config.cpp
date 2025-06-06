#include "../../libs/shared_ptr/shared_ptr.hpp"
#include "../../incs/Config/Config.hpp"
#include "../../incs/Config/ConfigElementFactory.hpp"
#include "../../incs/Log/Logger.hpp"
#include <fstream>

Config::Config(void)
    : _file_name(""), _element_ptr_vector(ElementPtrVector()) {}

Config::Config(const char *file_name)
    : _file_name(file_name), _element_ptr_vector(ElementPtrVector()) {
	try {
		this->_parse();
	} catch (const std::exception &e) {
		Logger::getInstance().error(e.what());
		throw (FailToParseException());
	}
}

Config::Config(const Config &ref)
    : _file_name(ref._file_name), _element_ptr_vector(ref._element_ptr_vector) {}

Config::~Config(void) {}

Config &Config::operator=(const Config &rhs) {
	if (this != &rhs) {
		this->~Config();
		new (this) Config(rhs);
	}
	return (*this);
}

const Config::ElementPtrVector &Config::getElementPtrVector(void) const {
	return (this->_element_ptr_vector);
}

bool Config::_parse(void) throw(std::exception) {
	std::ifstream infile(this->_file_name.c_str());
	std::string token;

	if (Config::invalidFileName(this->_file_name)) {
		throw (InvalidFileNameException());
	}
	
	if (!infile.is_open()) {
		throw (FailToOpenFileException());
	}
	
	while (infile >> token) {
		if (token != "server") {
			throw (InvalidSyntaxException());
		}
		this->_element_ptr_vector.push_back(
			ConfigElementFactory::getInstance().create("server", infile)
		);
	}

	if (this->_element_ptr_vector.size() == 0) {
		throw (InvalidArgumentException());
	}

	if (!infile.eof()) {
		throw (InvalidSyntaxException());
	}

	if (infile.is_open()) {
		infile.close();
	}
	
	return (true);
}

bool Config::invalidFileName(const std::string &file_name) {
	if (file_name.length() <= 5) {
		return (true);
	}

	if (file_name.substr(file_name.length() - 5, 5) != ".conf") {
		return (true);
	}

	return (false);
}

const char *Config::FailToParseException::what() const throw() {
	return ("Config: Fail to parse");
}

const char *Config::InvalidFileNameException::what() const throw() {
	return ("Config: Invalid file name");
}

const char *Config::FailToOpenFileException::what() const throw() {
	return ("Config: Fail to open file");
}

const char *Config::InvalidSyntaxException::what() const throw() {
	return ("Config: Invalid syntax");
}

const char *Config::InvalidArgumentException::what() const throw() {
	return ("Config: Invalid argument");
}
