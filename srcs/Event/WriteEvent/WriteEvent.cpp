#include "../../../incs/Event/WriteEvent/WriteEvent.hpp"
#if defined(__linux__)
  #include <sys/epoll.h>
#endif
#include "../../../incs/Log/Logger.hpp"
#include <unistd.h>
#include <cerrno>
#include <stdexcept>

WriteEvent::WriteEvent(WriteEventHandler* writeEventHandler)
    : Event(writeEventHandler)
{}

WriteEvent::~WriteEvent(void) {}

void WriteEvent::_onboardWrite(Event *event, int fd) {
    EventQueue &event_queue = EventQueue::getInstance();

#if defined(__APPLE__) || defined(__FreeBSD__)
    // kqueue 기반 구현
    EV_SET(
        event_queue.getEventSetElementPtr(),
        fd,
        EVFILT_WRITE,
        EV_ADD | EV_ENABLE,
        0,
        0,
        event
    );
    if (kevent(event_queue.getEventQueueFd(), 
               event_queue.getEventSet(), 
               1, NULL, 0, NULL) == -1) {
        throw (KqueueError());
    }
#elif defined(__linux__)
    // epoll 기반 구현 — 읽기 관심이 이미 있는 fd에도 안전 (병합 등록)
    event_queue.addInterest(fd, event, true);
#endif
}

void WriteEvent::_offboardWrite(Event *event, int fd) {
    EventQueue &event_queue = EventQueue::getInstance();

#if defined(__APPLE__) || defined(__FreeBSD__)
    // kqueue 기반 구현
    EV_SET(
        event_queue.getEventSetElementPtr(),
        fd,
        EVFILT_WRITE,
        EV_DELETE,
        0,
        0,
        event
    );
    if (kevent(event_queue.getEventQueueFd(), 
               event_queue.getEventSet(), 
               1, NULL, 0, NULL) == -1) {
        throw (KqueueError());
    }
    delete event;
#elif defined(__linux__)
    // epoll 기반 구현 — 읽기 관심이 남아 있으면 유지된다
    event_queue.removeInterest(fd, true);
    delete event;
#endif
}
