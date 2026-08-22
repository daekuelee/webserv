#include <Event/SpecialEvent/CgiKillEvent.hpp>
#include <Event/EventQueue/EventQueue.hpp>
#include <Event/Exception/KqueueError.hpp>
#include <unistd.h>
#include <stdexcept>
#include <errno.h>
#include <cstring>
#if defined(__APPLE__) || defined(__FreeBSD__)
  #include <sys/event.h>
#elif defined(__linux__)
  #include <sys/timerfd.h>
  #include <sys/epoll.h>
#endif
#include "../../../incs/Log/Logger.hpp"

CgiKillEvent::CgiKillEvent(pid_t cgiPid)
    : Event(new CgiKillEventHandler()), _cgiPid(cgiPid)
#if defined(__linux__)
    , _timerFd(-1)
#endif
{}

CgiKillEvent::~CgiKillEvent(void) {}

pid_t CgiKillEvent::getCgiPid(void) {
    return this->_cgiPid;
}

void CgiKillEvent::callEventHandler(void) {
    this->_event_handler->handleEvent(*this);
}

void CgiKillEvent::onboardQueue(void) {
    EventQueue &eventQueue = EventQueue::getInstance();
    Event *event = this;
#if defined(__APPLE__) || defined(__FreeBSD__)
    // kqueue 방식: EVFILT_TIMER를 사용하여 3분 타이머 등록
    EV_SET(
        eventQueue.getEventSetElementPtr(),
        1,                      // 임의 식별자 (타이머 FD 대신 사용)
        EVFILT_TIMER,
        EV_ADD | EV_ENABLE,
        0,
        3 * 60 * 1000,          // 3분 (밀리초 단위)
        event
    );
    if (kevent(eventQueue.getEventQueueFd(),
               eventQueue.getEventSet(),
               1, NULL, 0, NULL) == -1) {
        throw (KqueueError());
    }
#elif defined(__linux__)
    // Linux 방식: timerfd를 사용하여 3분 타이머 생성
    _timerFd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (_timerFd == -1) {
        throw std::runtime_error("CgiKillEvent: Failed to create timerfd");
    }
    struct itimerspec new_value;
    std::memset(&new_value, 0, sizeof(new_value));
    new_value.it_value.tv_sec = 3 * 60;  // 3분 후 만료
    new_value.it_value.tv_nsec = 0;
    new_value.it_interval.tv_sec = 0;    // one-shot 타이머
    new_value.it_interval.tv_nsec = 0;
    if (timerfd_settime(_timerFd, 0, &new_value, NULL) == -1) {
        close(_timerFd);
        throw std::runtime_error("CgiKillEvent: Failed to set timerfd");
    }
    // timerfd를 epoll에 등록하여 타이머 만료 시 이벤트 발생하도록 함 (레지스트리 경유)
    try {
        eventQueue.addInterest(_timerFd, event, false);
    } catch (...) {
        close(_timerFd);
        throw std::runtime_error("CgiKillEvent: Failed to add timerfd to epoll");
    }
#endif
}

void CgiKillEvent::offboardQueue(void) {
    EventQueue &eventQueue = EventQueue::getInstance();
    Event *event = this;
#if defined(__APPLE__) || defined(__FreeBSD__)
    EV_SET(
        eventQueue.getEventSetElementPtr(),
        1,
        EVFILT_TIMER,
        EV_DELETE,
        0,
        0,   // 타이머 삭제 시 시간 값은 무시됨
        event
    );
    if (kevent(eventQueue.getEventQueueFd(),
               eventQueue.getEventSet(),
               1, NULL, 0, NULL) == -1) {
        throw (KqueueError());
    }
    delete event;
#elif defined(__linux__)
    // Linux: 레지스트리에서 제거 후 timerfd 닫기
    eventQueue.removeInterest(_timerFd, false);
    close(_timerFd);
    _timerFd = -1;
    delete event;
#endif
}
