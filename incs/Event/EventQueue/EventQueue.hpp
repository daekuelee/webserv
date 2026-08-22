#ifndef EVENTQUEUE_HPP
# define EVENTQUEUE_HPP

#include <exception>

#if defined(__APPLE__) || defined(__FreeBSD__)
# include <sys/event.h>
typedef struct kevent event_t;
#elif defined(__linux__)
# include <sys/epoll.h>
typedef struct epoll_event event_t;
#else
# error "Unsupported platform for event handling"
#endif

#include "../EventBase/Event.hpp"
#include <vector>
#if defined(__linux__)
# include <map>
# include <utility>
#endif

class EventQueue {
	public:
		enum EventSetIndex {
			READ_SET,
			WRITE_SET
		};

	private:
		static const int MAX_EVENTS = 1024;
		static EventQueue *_instance;

	private:
		int _fd;
		event_t _ev_set;
		event_t _ev_list[MAX_EVENTS];

#if defined(__linux__)
	// epoll allows one registration per fd (kqueue registers per (fd, filter)
	// pair), so read/write interest on the same fd must be merged into one
	// registration and split back apart at dispatch time.
	public:
		struct FdInterest {
			int fd;
			Event *read;
			Event *write;
			FdInterest(void) : fd(-1), read(0), write(0) {}
		};

		// Merged-interest registration. `write` selects the direction slot.
		void addInterest(int fd, Event *event, bool write);
		void removeInterest(int fd, bool write);

	private:
		void _scrubDispatch(Event *event);

	private:
		std::map<int, FdInterest> _interest;
		// Regular files: epoll_ctl(ADD) refuses them with EPERM, but they are
		// always ready — dispatched every iteration instead of via epoll.
		std::vector<std::pair<int, Event *> > _alwaysReady;
		// Built once per pullEvents(); offboarded events are scrubbed to NULL
		// so a handler can safely delete an event dispatched later this round.
		std::vector<Event *> _dispatch;
		std::vector<int> _dispatchFd;
#endif

	private:
		EventQueue(void);

	public:
		~EventQueue(void);

	public:
		static EventQueue &getInstance(void);
		void deleteInstance(void);

	public:
		int pullEvents(void);
		bool pushEvent(Event *event);
		bool popEvent(Event *event);

	public:
		int getEventFd(int idx) const;
		int getEventQueueFd(void) const;
		Event *getEventData(int idx) const;
		event_t *getEventList(void);
		event_t *getEventSet(void);
		event_t *getEventSetElementPtr(void);

	public:
		class FailToCreateException: public std::exception {
			public:
				virtual const char *what() const throw();
		};

		class FailToControlException: public std::exception {
			public:
				virtual const char *what() const throw();
		};

		class FailToGetEventException: public std::exception {
			public:
				virtual const char *what() const throw();
		};

		class TimeoutException: public std::exception {
			public:
				virtual const char *what() const throw();
		};
};

#endif
