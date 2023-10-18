#include "../../../incs/Event/EventBase/EventFactory.hpp"
#include "../../../incs/Event/ReadEvent/ReadEventFromClient.hpp"
#include "../../../incs/Event/WriteEvent/WriteEventToClient.hpp"
#include "../../../incs/Event/ReadEvent/ReadEventFromCgi.hpp"
#include "../../../incs/Event/ListenEvent/ListenEvent.hpp"
#include <Event/WriteEvent/WriteEventToCache.hpp>
#include <Event/ReadEvent/ReadEventFromFile.hpp>
#include <Event/ReadEvent/ReadEventFromCache.hpp>
// #include "../../../incs/Event/WriteEvent/WriteEventToCgi.hpp"

EventFactory *EventFactory::_instance = NULL;
EventFactory::EventFactory(void) {}	
EventFactory::~EventFactory(void) {}
EventFactory &EventFactory::getInstance(void) {
	if (_instance == 0)
		_instance = new EventFactory();
	return (*_instance);
}
void EventFactory::DeleteInstance(void) {
	if (_instance != 0)
		delete _instance;
	_instance = NULL;
}
Event *EventFactory::createEvent(ft::EventType eventType, EventDto &eventDto){
	switch (eventType) {
		case ft::READ_EVENT_FROM_CLIENT:
			return (new ReadEventFromClient(eventDto.getChannel(),
			 eventDto.getVirtualServerManager())
			);
		case ft::WRITE_EVENT_TO_CLIENT:
			return (new WriteEventToClient(eventDto.getChannel(),
			 eventDto.getVirtualServerManager(),
			 eventDto.getHttpRequest())
			);
		case ft::FILE_READ_EVENT:
			return (new ReadEventFromFile(eventDto.getBuffer(),
			 eventDto.getPath(), eventDto.getMode())
			);
		// case ft::CACHE_READ_EVENT
		case ft::CACHE_WRITE_EVENT:
			return (new WriteEventToCache(eventDto.getBuffer(),
			 eventDto.getPath(), eventDto.getMode())
			);
		case ft::CACHE_READ_EVENT:
			return (new ReadEventFromCache(eventDto.getContent(),
			 eventDto.getPath(), eventDto.getMode())
			);

		// case ft::READ_EVENT_FROM_CGI:
		// 	return (new ReadEventFromCgi(eventDto));
		// case ft::WRITE_EVENT_TO_CGI:
		// 	return (new WriteEventToCgi(eventDto));
		case ft::LISTEN_EVENT:
			return (new ListenEvent(eventDto.getChannel(),
			 eventDto.getVirtualServerManager())
			);
		default:
			throw (FailToEventCreateException());
	}
}

// exception
const char *EventFactory::FailToEventCreateException::what() const throw() {
	return ("Fail to create Event");
}