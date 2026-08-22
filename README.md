# Webserv — Event-Driven HTTP/1.1 Server in C++98

A non-blocking, single-threaded HTTP/1.1 server built from C++98, the STL, and POSIX
system calls — nothing else. Started as a 42 Seoul team project (2023), ~19,000 lines.
The no-third-party constraint became the project's identity: the event loop, buffers,
cache, smart pointers, and type traits — every layer the server stands on — had to be
designed by hand.

Design references: RFCs and *HTTP: The Definitive Guide* for protocol structure, nginx
source for event handling / buffer chains / prefix routing, *The Linux Programming
Interface* and the epoll/kqueue man pages for the syscall layer.

## Features

HTTP/1.1 (GET/POST/PUT/DELETE, keep-alive, chunked transfer, multipart/form-data
uploads, cookies, custom error pages) · conditional GET with ETag over a hand-written
SHA-256 · CGI with process lifetime management · virtual hosting + trie-routed
locations · nginx-style config (`listen`, `server_name`, `root`, `alias`, `index`,
`error_page`, `client_max_body_size`, `location`, `return`, `autoindex`,
`allow_method`, `cgi_pass`) · epoll (Linux) / kqueue (macOS/BSD) behind one abstraction.

What follows is not the feature list — it's the design decisions and why they were made.

## Design decisions

### 1. Every unit of work is an event object

The most dangerous thing in a single-threaded server is code that blocks the loop
"just for a moment." This server blocks that structurally: the unit of work is not an
fd — it's an event object.

Under an abstract `Event`/`EventHandler` pair sit ~14 concrete event types: Read/Write
events for clients, files, CGI pipes, and the cache, plus scheduled events
(`CgiWaitEvent`, `CgiKillEvent`, `LogEvent`). Not just network I/O — **file reads, CGI
timeouts, and log flushes all pass through the same queue.** The question "does this
block the loop?" stops being a code-review item and becomes a property of the type
system: work that can't be expressed as an event can't enter the server.

`EventQueue` hides epoll and kqueue behind one interface. The two APIs differ in
registration model and event representation; those differences are quarantined inside
one class, so the event model above it doesn't know the platform.

```mermaid
flowchart LR
    EQ[EventQueue<br/>epoll / kqueue] --> LE[ListenEvent<br/>accept]
    EQ --> RE[Read events<br/>client / file / CGI / cache]
    EQ --> WE[Write events<br/>client / file / CGI / cache]
    EQ --> SE[Scheduled events<br/>CGI wait & kill, log flush]
    RE --> P[HTTP parser FSM]
    P --> R[Trie routing] --> PR[Pattern layer<br/>file read/write/delete · CGI · redirect]
    PR --> WE
```

### 2. Buffers — the traffic distribution chooses the data structure

Observation first: most HTTP requests fit in 4KB, but uploads and file transfers run
to tens of megabytes. One fixed buffer loses both ways — too small means repeated
reallocation, too big means over-allocation on every small request.

So the I/O buffer is a chain: one **4KB head node**, then **64KB nodes** linked behind
it as needed (`std::list<ft::shared_ptr<Node>>`). Reads append at the tail; writes
drain from the head; **a node whose bytes are fully sent is freed immediately.** A
small request lives and dies in one 4KB node; a 100MB transfer never holds its peak
memory. The same problem/solution pair appears in nginx's `ngx_chain_t` buffer chains,
which confirmed the direction.

### 3. File I/O — asynchronous end-to-end, with lifetime as synchronization

A large file must be served without ever stalling the loop, and concurrent access to
the same file must stay coherent — with no threads and no locks. The design answers
both with the same two tools: the event queue and reference counting.

**A readers-writer state machine per path.** `FileTableManager` (a passkey-gated
singleton) keeps a lazily-populated `std::map<path, FileData>`. Each `FileData` is a
tiny state machine: `{NoneProcessing, ReadingProcessing, WritingProcessing}` plus a
reader count. The crucial rule: **state transitions happen only inside RAII guard
constructors and destructors** —

- `SyncroFileDataAndReader` ctor: `readerCount++`; the 0→1 transition flips the path
  to `ReadingProcessing`. Its dtor: `readerCount--`; the →0 transition flips back to
  `NoneProcessing`.
- `SyncroFileDataAndWriter` ctor: path becomes `WritingProcessing` (exclusive); dtor
  releases it.

The guards are handed out as `ft::shared_ptr`, so holding one *is* holding the
permission. There is no `unlock()` anywhere in the codebase to forget.

The admission rules fall out of the states: readers may enter while the path is
`NoneProcessing` or `ReadingProcessing` (N concurrent readers share), and must wait
during `WritingProcessing` — after a mutation, buffered progress would be garbage. A
writer is stricter: it needs `NoneProcessing`, i.e. it waits for **all** readers to
drain and for any other writer, whether the current holder is itself or someone else.

**The async read path, step by step.** For a file above the cache block size:

1. `FileManager` checks the path's state; a writer in flight → report *should-wait*.
2. Otherwise it builds a reader guard and hands it to a newly created
   `ReadEventFromFile`. **The guard's owner is the event itself.**
3. The file fd joins the event queue. Each readiness firing does one
   `buffer->ioRead(fd)` — one chunk appended to the response's buffer chain — and
   advances an offset. Between chunks, the loop serves everyone else.
4. When the offset reaches the file size, the event offboards and is destroyed; the
   guard dies with it, the reader count drops. **Nobody released anything — the
   permission ended because the work's lifetime ended.**
5. The client's response path re-polls progress through its own events, tracking a
   per-response sync state (`NotSetting → Reading → ReadingDone`): when the buffered
   byte count converges to the file size, transmission starts and the chain drains to
   the socket. Completion is detected by convergence, not by a callback — there is no
   callback registry to corrupt.

Writes mirror this with a writer guard and a `WriteEventToFile` draining the request
body to disk on writability events; the uploading request tracks its own
`Writing → WritingDone` state and recognizes completion when the path returns to
`NoneProcessing` while its own state still says `Writing` — "the write I started has
finished" and "someone else's write finished" are distinguishable without any shared
flag beyond the state machine.

**Small files take the cache path — with coherence handled.** Files under the cache
block size are served from an LRU cache (list + map, 4KB blocks). A hit answers from
memory immediately. A miss registers the entry and populates it through the same event
machinery — and a second request arriving mid-population sees the entry's in-progress
status and simply waits instead of re-registering: **concurrent misses on one file
coalesce into a single disk read** (the cache-stampede problem, handled at the design
level). Mutations keep the cache honest: a small write to a cached file is applied
write-through into the cache; a write that outgrows the block **evicts the entry and
falls back to the async file path**; DELETE evicts too. Size-crossing in both
directions is considered — a file can grow out of, or shrink into, cacheability
without serving stale bytes.

### 4. CGI — if you fork it, you own it to the end

CGI runs via fork/execve with non-blocking pipe capture; the real problem isn't
execution, it's cleanup. An unresponsive script must die by gateway timeout, and a
killed process must be reaped or it lingers as a zombie.

Both are events. `CgiWaitEvent` reaps children with a non-blocking `waitpid(WNOHANG)`
sweep; a scheduled `CgiKillEvent` sends SIGKILL past the timeout. Process lifetime
management is a first-class citizen of the event loop — the server can run
indefinitely without accumulating zombies.

### 5. Filling C++98's gaps by hand — `libs/`

C++98 has no `shared_ptr`, no `optional`, no type traits. The missing pieces were
built from scratch — and then actually used as the server's skeleton, which is the
part that matters.

**`ft::shared_ptr`** — used across 130 files; object lifetime in this codebase *is*
reference counting. The layout is two words (`T* _ptr`, `int* _count`), and the
interesting machinery is in the templates:

- A **converting copy constructor** `template <typename U> shared_ptr(const
  shared_ptr<U>&)`, enabled by cross-instantiation friendship (`template <typename U>
  friend class shared_ptr`), lets a `shared_ptr<Derived>` become a
  `shared_ptr<Base>` while sharing the same count — which is what lets the event
  system pass concrete events around as their abstract interfaces.
- An **aliasing constructor** `shared_ptr(const shared_ptr<U>& ref, T* ptr)` shares
  `ref`'s count while pointing at a different object — the standard-library trick that
  makes `static_pointer_cast` correct: the cast result keeps the original object
  alive, no second control count, no double delete.
- Assignment is destroy-then-placement-new (`this->~shared_ptr(); new (this)
  shared_ptr(ref);`) — self-assignment-guarded reuse of the copy constructor as the
  single source of truth for what assignment means.
- C++98 has no variadic templates, so `make_shared` is an overload family
  (0–3 arguments) instead.

**The passkey idiom, twice.** Buffer nodes and the file table are gated by a
zero-size `AccessKey` type whose constructor is private and whose `friend` is the one
legitimate owner (`IoOnlyReadBuffer` for nodes, `FileManager` for the table). Any call
site must present a key it cannot construct — so "who may touch this" is enforced by
the compiler, not by a comment. The same technique guards `HttpResponse`'s internal
buffers throughout the file pipeline.

**Type traits from STL source analysis** — reading the gcc C++98 STL raised the
question of how containers dispatch on types without overloading chaos; the answer
(SFINAE) was reimplemented in `libs/Library/Type.hpp`: `enable_if`,
`integral_constant`, `is_same`, `is_integral`, and friends, in C++98 syntax.

**`ft::Trie`** — drives location routing: `longestPrefixSearch` walks the URI once
and returns the most specific `location` block in O(URI length), independent of how
many routes the config defines.

**A hash table nobody asked for** — prime-sized table with **double hashing**
(`h2(h1)` as the probe step) and a sieve-cached prime list up to 10⁶ for resizing,
plus a `std::string` specialization. It's a standalone study piece — implemented,
tested, and honestly labeled: not wired into the server.

Plus `Optional`, `unique_ptr`, and an `Assert` utility with scoped enable/disable.

### 6. Logging doesn't block either

The logger buffers 32KB and flushes through a `LogEvent` in the same queue as network
I/O — same rules as everything else. A slow disk can never make logging delay a
response.

## Request lifecycle

1. **Accept** — `ListenEvent` fires on a ready listening socket (multiple `listen`
   sockets across ports/addresses supported); each accepted fd is set non-blocking and
   registered.
2. **Parse** — `ReadEventFromClient` feeds the parser, an explicit state machine
   (`BEFORE → START_LINE → HEADERS → BODY → FINISH`) that carries partial reads across
   event boundaries. Body framing branches three ways: `Content-Length`, chunked
   decoding, multipart/form-data (its own boundary FSM). Malformed or oversized input
   raises a typed HTTP exception (400, 413, …) that becomes an error response — error
   and success travel the same pipeline.
3. **Route** — trie longest-prefix match against `location` blocks; `Host` header
   selects the virtual server among `server_name`s.
4. **Generate** — the Pattern layer dispatches per outcome, each with its response
   builder: static file (cache or the async read path above), directory listing when
   autoindex is on / index-file fallback when off, upload storage, `return` redirects,
   DELETE, CGI.
5. **Transmit** — the response is staged in the buffer chain; `WriteEventToClient`
   drains it as the socket accepts bytes. Keep-alive returns to step 2 on the same
   socket; error/`Connection: close` paths close after the final bytes.

## Configuration

The grammar the parser actually accepts. Two parser rules to know before copying:
**paths must be absolute** (relative paths are rejected), and **`#` comments are not
supported** — a config containing one fails to parse. `listen` takes either a port or
`ip:port` (e.g. `127.0.0.1:80`), and `alias` replaces the matched location prefix
(vs `root`, which prepends).

```nginx
server {
    listen 8080;
    server_name www.example.com example.com;
    root /var/www/html;
    index index.html index.htm;
    error_page 404 /404.html;
    client_max_body_size 8M;

    location / {
        autoindex on;
    }

    location /redirect {
        return 301 /images;
    }

    location /images {
        alias /var/www/img;
        allow_method GET POST DELETE;
        autoindex on;
    }

    location /cgi {
        cgi_pass on;
        alias /var/www/cgi;
        allow_method GET POST;
    }
}
```

## Build & Run

Requires a C++98-capable compiler and Make (tested with GCC/Clang on Linux and macOS).

```bash
git clone https://github.com/42-webserver/webserv.git
cd webserv
make            # targets: all · clean · fclean · re · sanitize (ASan build)
./run.sh        # generates config/webserv.conf from the template with this
                # clone's absolute paths, then serves on :8080
```

`run.sh` exists because the config parser accepts absolute paths only — it fills
`config/webserv.conf.template` (`@ROOT@` placeholders) with `$(pwd)` and starts the
server against the demo fixtures in `test/` and `html/`. To run a hand-written config
directly: `./webserv <config file>`. Binding ports below 1024 needs elevated privileges.

## Testing

- **Browser / curl**: static pages, autoindex listings, custom error pages,
  `curl -F 'file=@photo.jpg' http://localhost:8080/images/` (multipart upload),
  `curl -X DELETE`, CGI GET/POST.
- **Raw protocol**: `telnet localhost 8080` with hand-typed requests to inspect status
  lines, headers, chunked framing.
- **Load**: `ab -n 1000 -c 100 http://localhost:8080/` — the single-threaded loop
  serves concurrent clients without stalling on large transfers.
- **Unit-style tests** live next to their modules (`srcs/*/Test/`,
  `libs/Library/Test/`) as standalone compilation units; `test/` holds manual fixtures
  (chunked bodies, upload files, a request script).

UML: [class diagram](assets/Class%20diagram.png) ·
[sequence diagram](assets/Sequence%20diagram.png) (StarUML source in `assets/`).

## Team

42 Seoul group project — [daekuelee](https://github.com/daekuelee) (event system,
buffer, file manager/LRU, CGI execution, library layer),
[Younganswer](https://github.com/Younganswer) (config parsing, server/vhost layer),
wken5577 (HTTP parsing/response, shared). HTTP engine work was shared across the team.
