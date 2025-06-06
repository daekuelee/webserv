#include "../../incs/Config/ReturnElement.hpp"
#include "../../incs/Log/Logger.hpp"

ReturnElement::ReturnElement(void) {}

ReturnElement::ReturnElement(std::ifstream &infile) throw(std::exception) {
	try {
		this->_parse(infile);
	} catch (const std::exception &e) {
		Logger::getInstance().error(e.what());
		throw (FailToParseException());
	}
}

ReturnElement::ReturnElement(const ReturnElement &ref)
	: ConfigElement(ref), _code(ref._code), _uri(ref._uri) {}

ReturnElement::~ReturnElement(void) {}

ReturnElement &ReturnElement::operator=(const ReturnElement &rhs) {
	if (this != &rhs) {
		this->~ReturnElement();
		new (this) ReturnElement(rhs);
	}
	return (*this);
}

bool ReturnElement::_parse(std::ifstream &infile) throw(std::exception) {
	std::string token;

	if (!(infile >> token)) {
		throw (InvalidSyntaxException());
	}
	for (size_t i = 0; i < token.length(); i++) {
		if (!std::isdigit(token[i])) {
			throw (InvalidArgumentException());
		}
	}
	this->_code = std::atoi(token.c_str());
	if (this->_codeIsNotValid(this->_code)) {
		throw (InvalidArgumentException());
	}

	// ⛏ back() 대신 인덱스 사용, token 비어있는지 먼저 확인
	if (!(infile >> token) || token.empty() || token[token.length() - 1] != ';') {
		throw (InvalidSyntaxException());
	}

	this->_uri = token.substr(0, token.length() - 1);
	if (this->_uriIsNotValid(this->_uri)) {
		throw (InvalidArgumentException());
	}
	return (true);
}

bool ReturnElement::_codeIsNotValid(int code) {
	return (code < 300 || 400 <= code);
}

bool ReturnElement::_uriIsNotValid(const std::string &uri) {
	(void)uri;
	return (false);
}

int ReturnElement::getCode(void) const {
	return (this->_code);
}

const std::string &ReturnElement::getUri(void) const {
	return (this->_uri);
}

const char *ReturnElement::FailToParseException::what(void) const throw() {
	return ("ReturnElement: Fail to Parse");
}

const char *ReturnElement::InvalidSyntaxException::what(void) const throw() {
	return ("ReturnElement: Invalid Syntax");
}

const char *ReturnElement::InvalidArgumentException::what(void) const throw() {
	return ("ReturnElement: Invalid Argument");
}

std::ostream &operator<<(std::ostream &os, const ReturnElement &rhs) {
	os << rhs.getCode() << " " << rhs.getUri();
	return (os);
}
