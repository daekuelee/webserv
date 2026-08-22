#include "../../../incs/Event/EventQueue/EventQueue.hpp"
#include <new>
#include <stddef.h>
#include <unistd.h>
#include <iostream>
#include <errno.h>
#include "../../../incs/Log/Logger.hpp"
#include <fcntl.h>
#if defined(__linux__)
#include <sys/epoll.h>
#endif

// 정적 멤버 초기화
EventQueue *EventQueue::_instance = NULL;

EventQueue::EventQueue(void) {
#if defined(__APPLE__) || defined(__FreeBSD__)
    // kqueue 기반 구현 (원래 코드)
    this->_fd = kqueue();
    if (this->_fd == -1) {
        throw (FailToCreateException());
    }
#elif defined(__linux__)
    // epoll 기반 구현
    this->_fd = epoll_create1(0);
    if (this->_fd == -1) {
        throw (FailToCreateException());
    }
#endif
}

EventQueue::~EventQueue(void) {
    if (this->_fd != -1)
        close(this->_fd);
}

EventQueue &EventQueue::getInstance(void) {
    if (EventQueue::_instance == NULL) {
        EventQueue::_instance = new EventQueue();
    }
    return (*EventQueue::_instance);
}

void EventQueue::deleteInstance(void) {
    if (EventQueue::_instance) {
        close(EventQueue::_instance->_fd);
        delete EventQueue::_instance;
        EventQueue::_instance = NULL;
    }
}

int EventQueue::getEventFd(int idx) const {
#if defined(__APPLE__) || defined(__FreeBSD__)
    return (this->_ev_list[idx].ident);
#elif defined(__linux__)
    // 등록은 data.ptr을 쓰므로 (epoll_data는 union) data.fd는 유효하지 않다 —
    // 디스패치 리스트와 나란히 기록해 둔 fd를 반환
    return (this->_dispatchFd[idx]);
#endif
}

int EventQueue::getEventQueueFd(void) const {
    return (this->_fd);
}

Event *EventQueue::getEventData(int idx) const {
#if defined(__APPLE__) || defined(__FreeBSD__)
    return static_cast<Event *>(this->_ev_list[idx].udata);
#elif defined(__linux__)
    // pullEvents()가 만든 디스패치 리스트에서 반환.
    // 이번 라운드에 offboard된 이벤트는 NULL로 스크럽되어 있을 수 있다.
    return (this->_dispatch[idx]);
#endif
}

event_t *EventQueue::getEventList(void) {
    return (this->_ev_list);
}

event_t *EventQueue::getEventSet(void) {
    return (&this->_ev_set);
}

event_t *EventQueue::getEventSetElementPtr(void) {
    return (&this->_ev_set);
}

int EventQueue::pullEvents(void) {
    int ret = 0;
#if defined(__APPLE__) || defined(__FreeBSD__)
    ret = kevent(this->_fd, NULL, 0, this->_ev_list, MAX_EVENTS, NULL);
    if (ret == -1) {
        throw (FailToGetEventException());
    }
    return ret;
#elif defined(__linux__)
    // 항상-ready인 파일 이벤트가 대기 중이면 블록하지 않는다
    int timeout = this->_alwaysReady.empty() ? -1 : 0;
    ret = epoll_wait(this->_fd, this->_ev_list, MAX_EVENTS, timeout);
    if (ret == -1) {
        // SIGCHLD(CGI 종료)는 SA_RESTART로도 epoll_wait을 재시작하지 않는다 —
        // EINTR은 치명 에러가 아니라 빈 라운드로 취급
        if (errno == EINTR)
            ret = 0;
        else
            throw (FailToGetEventException());
    }

    this->_dispatch.clear();
    this->_dispatchFd.clear();
    for (int i = 0; i < ret; ++i) {
        FdInterest *entry = static_cast<FdInterest *>(this->_ev_list[i].data.ptr);
        unsigned int evs = this->_ev_list[i].events;
        if (entry->read && (evs & (EPOLLIN | EPOLLERR | EPOLLHUP))) {
            this->_dispatch.push_back(entry->read);
            this->_dispatchFd.push_back(entry->fd);
        }
        if (entry->write && (evs & (EPOLLOUT | EPOLLERR | EPOLLHUP))) {
            this->_dispatch.push_back(entry->write);
            this->_dispatchFd.push_back(entry->fd);
        }
    }
    for (size_t i = 0; i < this->_alwaysReady.size(); ++i) {
        this->_dispatch.push_back(this->_alwaysReady[i].second);
        this->_dispatchFd.push_back(this->_alwaysReady[i].first);
    }
    return static_cast<int>(this->_dispatch.size());
#endif
}

#if defined(__linux__)
void EventQueue::addInterest(int fd, Event *event, bool write) {
    std::map<int, FdInterest>::iterator it = this->_interest.find(fd);

    if (it == this->_interest.end()) {
        FdInterest entry;
        entry.fd = fd;
        if (write) entry.write = event; else entry.read = event;
        it = this->_interest.insert(std::make_pair(fd, entry)).first;

        struct epoll_event ev;
        ev.events = (it->second.read ? EPOLLIN : 0u) | (it->second.write ? EPOLLOUT : 0u);
        ev.data.ptr = &it->second;
        if (epoll_ctl(this->_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
            this->_interest.erase(it);
            if (errno == EPERM) {
                // 일반 파일: epoll이 등록을 거부하지만 항상 ready — 별도 리스트로
                this->_alwaysReady.push_back(std::make_pair(fd, event));
                return;
            }
            throw (FailToControlException());
        }
        return;
    }

    // fd가 이미 등록됨 → 관심 병합 (기존 코드의 EEXIST 지점)
    if (write) it->second.write = event; else it->second.read = event;
    struct epoll_event ev;
    ev.events = (it->second.read ? EPOLLIN : 0u) | (it->second.write ? EPOLLOUT : 0u);
    ev.data.ptr = &it->second;
    if (epoll_ctl(this->_fd, EPOLL_CTL_MOD, fd, &ev) == -1) {
        throw (FailToControlException());
    }
}

void EventQueue::removeInterest(int fd, bool write) {
    // 항상-ready 리스트(일반 파일)에서 먼저 찾는다
    for (size_t i = 0; i < this->_alwaysReady.size(); ++i) {
        if (this->_alwaysReady[i].first == fd) {
            this->_scrubDispatch(this->_alwaysReady[i].second);
            this->_alwaysReady.erase(this->_alwaysReady.begin() + i);
            return;
        }
    }

    std::map<int, FdInterest>::iterator it = this->_interest.find(fd);
    if (it == this->_interest.end())
        return;

    Event *removed = write ? it->second.write : it->second.read;
    if (write) it->second.write = 0; else it->second.read = 0;
    if (removed)
        this->_scrubDispatch(removed);

    if (it->second.read == 0 && it->second.write == 0) {
        if (epoll_ctl(this->_fd, EPOLL_CTL_DEL, fd, NULL) == -1) {
            this->_interest.erase(it);
            throw (FailToControlException());
        }
        this->_interest.erase(it);
        return;
    }

    struct epoll_event ev;
    ev.events = (it->second.read ? EPOLLIN : 0u) | (it->second.write ? EPOLLOUT : 0u);
    ev.data.ptr = &it->second;
    if (epoll_ctl(this->_fd, EPOLL_CTL_MOD, fd, &ev) == -1) {
        throw (FailToControlException());
    }
}

void EventQueue::_scrubDispatch(Event *event) {
    // 이번 라운드에 아직 디스패치되지 않은 항목이 삭제될 수 있으므로 NULL 처리
    for (size_t i = 0; i < this->_dispatch.size(); ++i) {
        if (this->_dispatch[i] == event)
            this->_dispatch[i] = 0;
    }
}
#endif

bool EventQueue::pushEvent(Event *event) {
    try {
        event->onboardQueue();
    } catch (const std::exception &e) {
        Logger::getInstance().error(e.what());
        return false;
    }
    return true;
}

bool EventQueue::popEvent(Event *event) {
    try {
        event->offboardQueue();
    } catch (const std::exception &e) {
        Logger::getInstance().error(e.what());
        return false;
    }
    return true;
}

// Exception 구현
const char *EventQueue::FailToCreateException::what(void) const throw() {
    return ("EventQueue: Fail to create");
}

const char *EventQueue::FailToControlException::what(void) const throw() {
    return ("EventQueue: Fail to control");
}

const char *EventQueue::FailToGetEventException::what(void) const throw() {
    return ("EventQueue: Fail to get event");
}

const char *EventQueue::TimeoutException::what(void) const throw() {
    return ("EventQueue: Timeout");
}
