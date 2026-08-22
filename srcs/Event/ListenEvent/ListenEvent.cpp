#include "../../../incs/Event/ListenEvent/ListenEvent.hpp"

#if defined(__APPLE__) || defined(__FreeBSD__)
  #include <sys/event.h>
#elif defined(__linux__)
  #include <sys/epoll.h>
#endif

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <stdexcept>
#include <iostream>
#include "../../../incs/Log/Logger.hpp"

ListenEvent::ListenEvent(ft::shared_ptr<Channel> channel, ft::shared_ptr<VirtualServerManager> virtual_server_manager)
    : Event(new ListenEventHandler()),
      SingleStreamable(channel),
      _virtualServerManager(virtual_server_manager)
{}

ListenEvent::~ListenEvent(void) {}

void ListenEvent::callEventHandler(void) { 
    this->_event_handler->handleEvent(*this);
}

void ListenEvent::onboardQueue(void) {
    EventQueue &event_queue = EventQueue::getInstance();
    int fd = this->getFd();
    Event *event = this;

    // 비동기(non-blocking) 설정
    try {
        this->getChannel().get()->setNonBlocking();
    } catch (const std::exception &e) {
        Logger::getInstance().error(e.what());
        Logger::getInstance().error("Fail to accept client");
        throw (Event::FailToOnboardException());
    }

#if defined(__APPLE__) || defined(__FreeBSD__)
    // kqueue 기반 구현
    EV_SET(
        event_queue.getEventSetElementPtr(),
        fd,
        EVFILT_READ,
        EV_ADD | EV_ENABLE,
        0,
        0,
        event
    );
    
    // 로컬 포트 획득
    struct sockaddr_in localAddress;
    socklen_t addressLength = sizeof(localAddress);
    if (getsockname(fd, (struct sockaddr*)&localAddress, &addressLength) == -1) {
        perror("getsockname");
        throw std::runtime_error("getsockname");
    }
    int localPort = ntohs(localAddress.sin_port);
    ft::shared_ptr<VirtualServerManager> vsm = this->getVirtualServerManager();
    vsm->setPort(localPort);
    
    if (kevent(event_queue.getEventQueueFd(), event_queue.getEventSet(), 1, NULL, 0, NULL) == -1) {
        perror("kevent");
        throw (KqueueError());
    }
#elif defined(__linux__)
    // 로컬 포트 획득
    struct sockaddr_in localAddress;
    socklen_t addressLength = sizeof(localAddress);
    if (getsockname(fd, (struct sockaddr*)&localAddress, &addressLength) == -1) {
        perror("getsockname");
        throw std::runtime_error("getsockname");
    }
    int localPort = ntohs(localAddress.sin_port);
    ft::shared_ptr<VirtualServerManager> vsm = this->getVirtualServerManager();
    vsm->setPort(localPort);

    event_queue.addInterest(fd, event, false);
#endif
}

void ListenEvent::offboardQueue(void) {
    EventQueue &event_queue = EventQueue::getInstance();

#if defined(__APPLE__) || defined(__FreeBSD__)
    EV_SET(
        event_queue.getEventSetElementPtr(),
        this->getFd(),
        EVFILT_READ,
        EV_DELETE,
        0,
        0,
        this
    );
    
    if (kevent(event_queue.getEventQueueFd(), event_queue.getEventSet(), 1, NULL, 0, NULL) == -1) {
        throw (KqueueError());
    }
#elif defined(__linux__)
    event_queue.removeInterest(this->getFd(), false);
#endif
    delete this;
}

ft::shared_ptr<VirtualServerManager> ListenEvent::getVirtualServerManager(void) const { 
    return (this->_virtualServerManager);
}

// Exception 구현
const char *ListenEvent::FailToAcceptException::what(void) const throw() { 
    return ("ListenEvent: Fail to accept");
}

const char *ListenEvent::FailToControlException::what(void) const throw() { 
    return ("ListenEvent: Fail to control");
}
