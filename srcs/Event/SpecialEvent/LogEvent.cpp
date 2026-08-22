#include "../../../incs/Event/SpecialEvent/LogEvent.hpp"
#include "../../../incs/Log/Logger.hpp"
#include "../../../incs/Event/EventQueue/EventQueue.hpp"
#include "../../../incs/Event/Exception/KqueueError.hpp"
#include <cstring>
#include <stdexcept>
#include <unistd.h>
#if defined(__linux__)
#include <sys/timerfd.h>
#include <sys/epoll.h>
#endif

LogEvent::LogEvent(void)
    : Event(new LogEventHandler())
#if defined(__linux__)
    , _timerFd(-1)
#endif
{}

LogEvent::~LogEvent(void) {
#if defined(__linux__)
    if (_timerFd != -1) {
        close(_timerFd);
    }
#endif
}

void LogEvent::callEventHandler(void) { 
    this->_event_handler->handleEvent(*this);
}

void LogEvent::onboardQueue(void) {
    EventQueue& eventQueue = EventQueue::getInstance();
    Event *event = this;

#if defined(__APPLE__) || defined(__FreeBSD__)
    // kqueue 기반 구현: 3분 타이머 (3 * 60 * 1000ms)
    EV_SET(
        eventQueue.getEventSetElementPtr(),
        1,
        EVFILT_TIMER,
        EV_ADD | EV_ENABLE,
        0,
        3 * 60 * 1000,
        event
    );
    if (kevent(eventQueue.getEventQueueFd(),
               eventQueue.getEventSet(),
               1, NULL, 0, NULL) == -1) {
        throw (KqueueError());
    }
#elif defined(__linux__)
    // Linux 구현: timerfd와 epoll을 사용하여 3분 타이머를 구성
    _timerFd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (_timerFd == -1) {
        throw std::runtime_error("LogEvent: Failed to create timerfd");
    }
    struct itimerspec new_value;
    std::memset(&new_value, 0, sizeof(new_value));
    // 초기 만료 시간을 3분(180초)으로 설정
    new_value.it_value.tv_sec = 3 * 60;
    new_value.it_value.tv_nsec = 0;
    // one-shot 타이머 (반복 없음)
    new_value.it_interval.tv_sec = 0;
    new_value.it_interval.tv_nsec = 0;
    if (timerfd_settime(_timerFd, 0, &new_value, NULL) == -1) {
        throw std::runtime_error("LogEvent: Failed to set timerfd");
    }
    // timerfd를 epoll에 등록 (읽기 이벤트, 레지스트리 경유)
    eventQueue.addInterest(_timerFd, event, false);
#endif
}

void LogEvent::offboardQueue(void) {
    EventQueue& eventQueue = EventQueue::getInstance();
    Event *event = this;

#if defined(__APPLE__) || defined(__FreeBSD__)
    EV_SET(
        eventQueue.getEventSetElementPtr(),
        1,
        EVFILT_TIMER,
        EV_DELETE,
        0,
        3 * 1000,
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
