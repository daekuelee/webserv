#include "../../libs/shared_ptr/shared_ptr.hpp"
#include "../../incs/Event/EventQueue/EventQueue.hpp"
#include "../../incs/Server/Webserv.hpp"
#include "../../incs/Event/EventBase/Event.hpp"
#include "../../incs/Log/Logger.hpp"
#include <unistd.h>
#include <stdlib.h>
#include <typeinfo>
Webserv::Webserv(void): _physical_server_manager() {}
Webserv::~Webserv(void) {}
Webserv::Webserv(const Webserv &ref): _physical_server_manager(ref._physical_server_manager) {}
Webserv	&Webserv::operator=(const Webserv &rhs) {
	if (this != &rhs) {
		this->~Webserv();
		new (this) Webserv(rhs);
	}
	return (*this);
}

bool	Webserv::run(const Config &config) {
	try {
		this->_physical_server_manager.build(config);
		this->_physical_server_manager.run();
		//std::cerr << "Webserv is running..." << std::endl;
		//std::cout << *this << std::endl;
		//exit(0);
	} catch (const std::exception &e) {
		Logger::getInstance().error(e.what());
		throw (FailToRunException());
	}

	while (true) {
		int		event_length;
		Event	*event_data;
		try {
			event_length = EventQueue::getInstance().pullEvents();
		} catch (const std::exception &e) {
			Logger::getInstance().error(e.what());
			throw (FailToRunException());
		}

		for (int i=0; i<event_length; i++) {
			event_data = EventQueue::getInstance().getEventData(i);
			// 이번 라운드에 앞선 핸들러가 offboard한 이벤트는 NULL로 스크럽됨
			if (event_data)
				event_data->callEventHandler();
		}
	}
	return (true);
}

const char	*Webserv::FailToRunException::what() const throw() { return ("Webserv: Fail to run"); }

std::ostream	&operator<<(std::ostream &os, const Webserv &webserv) {
	os << "Webserv:\n";
	os << webserv._physical_server_manager;
	return (os);
}