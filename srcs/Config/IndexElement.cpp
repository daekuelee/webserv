#include "../../incs/Config/IndexElement.hpp"
#include "../../incs/Log/Logger.hpp"
#include <algorithm>

IndexElement::IndexElement(void) {}

IndexElement::IndexElement(std::ifstream &infile) throw(std::exception) {
	try {
		this->_parse(infile);
	} catch (const std::exception &e) {
		Logger::getInstance().error(e.what());
		throw (FailToParseException());
	}
}

IndexElement::IndexElement(const IndexElement &ref)
    : ConfigElement(ref), _uris(ref._uris) {}

IndexElement::~IndexElement(void) {}

IndexElement	&IndexElement::operator=(const IndexElement &rhs) {
	if (this != &rhs) {
		this->~IndexElement();
		new (this) IndexElement(rhs);
	}
	return (*this);
}

bool IndexElement::_parse(std::ifstream &infile) throw(std::exception) {
	std::string token;

	while (infile >> token) {
		if (!token.empty() && token[token.size() - 1] == ';') {
			token = token.substr(0, token.length() - 1);
			if (this->_uriIsNotValid(token)) {
				throw (InvalidArgumentException());
			}
			if (std::find(this->_uris.begin(), this->_uris.end(), token) == this->_uris.end()) {
				this->_uris.push_back(token);
			}
			break;
		}
		if (this->_uriIsNotValid(token)) {
			throw (InvalidArgumentException());
		}
		if (std::find(this->_uris.begin(), this->_uris.end(), token) == this->_uris.end()) {
			this->_uris.push_back(token);
		}
	}
	if (this->_uris.empty()) {
		throw (InvalidSyntaxException());
	}
	return (true);
}

bool IndexElement::_uriIsNotValid(const std::string &uri) { (void) uri; return (false); }

const std::vector<std::string>	&IndexElement::getUris(void) const { return (this->_uris); }

const char *IndexElement::FailToParseException::what(void) const throw() { return ("IndexElement: Fail to Parse"); }
const char *IndexElement::InvalidSyntaxException::what(void) const throw() { return ("IndexElement: Invalid Syntax"); }
const char *IndexElement::InvalidArgumentException::what(void) const throw() { return ("IndexElement: Invalid Argument"); }

std::ostream	&operator<<(std::ostream &os, const IndexElement &rhs) {
	for (std::vector<std::string>::const_iterator it = rhs.getUris().begin(); it != rhs.getUris().end(); ++it) {
		os << *it << " ";
	}
	return (os);
}
