---
title: Proxy Lab
description: Designing a caching, concurrent HTTP proxy — sockets, threads, and a reader–writer cache.
---

# Proxy Lab · A Concurrent Web Proxy

<p class="article-meta">Network programming <span class="dot">·</span> <span class="pill pill--wip">In progress</span> <span class="dot">·</span> <a href="https://github.com/LeeXSean/CSAPP-Lab-Solutions-lx/tree/main/Proxy_Lab">Handout</a></p>

!!! warning "Status: not yet implemented"
    This is the course finale, and it's still on my desk — `proxy.c` currently holds only the skeleton. What follows is the **design** I'm building toward, not a walkthrough of finished code. I'll replace it with a full, annotated implementation once it passes the driver.

A proxy sits between a browser and the web: it accepts the browser's HTTP request, forwards it to the origin server, and streams the reply back — optionally remembering recent replies so it can answer the next identical request itself. Building one ties together everything the course has taught about **sockets, concurrency, and synchronization**.

!!! abstract "The assignment"
    Build the proxy in three escalating stages:

    1. A **sequential** proxy that correctly forwards one HTTP GET at a time.
    2. A **concurrent** proxy that serves many clients simultaneously.
    3. A **caching** proxy that stores recent objects (≤ 100 KB each, ≤ ~1 MB total) and serves hits without contacting the origin.

## The request path

``` text
                  GET http://host/path                 hit
   +---------+ ---------------------->  +-------+ ------>  +--------------+
   | browser |                          | proxy |          | object cache |
   +---------+ <----------------------  +-------+ <------  +--------------+
                   response (cached)         |  ^
                                        miss |  | response
                                             v  |
                                        +---------------+
                                        | origin server |
                                        +---------------+
```

---

## Stage 1 · A sequential proxy

The backbone is a classic accept loop. For each connection: read and parse the request line, rebuild a clean request for the origin, connect, forward, and relay the response byte-for-byte.

``` c
int listenfd = Open_listenfd(port);
while (1) {
    int connfd = Accept(listenfd, ...);
    doit(connfd);           // parse -> forward -> relay
    Close(connfd);
}
```

The care lives in `doit`:

- **Parse** the request line `GET http://host:port/path HTTP/1.1`, splitting out host, port, and path.
- **Rebuild headers** — force `HTTP/1.0`, send the required `Host`, `User-Agent`, `Connection: close`, and `Proxy-Connection: close`, and pass through the rest.
- **Relay** the origin's response back to the client with robust I/O, since a response can arrive in arbitrarily sized chunks.

!!! note "Robust I/O (RIO) is non-negotiable"
    Network reads and writes return *short counts* — fewer bytes than asked — at any moment. Every transfer goes through the CS:APP `rio_*` buffered wrappers, which loop until the full amount is moved or EOF is reached. A raw `read`/`write` here silently truncates pages.

## Stage 2 · Making it concurrent

A sequential proxy stalls every client behind the slowest origin. The plan is **one thread per connection**, detached so it cleans up after itself:

``` c
pthread_t tid;
int *connfdp = Malloc(sizeof(int));   // (1) heap-per-thread, no shared stack slot
*connfdp = Accept(listenfd, ...);
Pthread_create(&tid, NULL, thread, connfdp);

void *thread(void *vargp) {
    int connfd = *(int *)vargp;
    Pthread_detach(pthread_self());   // (2) auto-reap; no join needed
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}
```

1. Each thread gets its connection fd through its **own** heap cell — passing `&connfd` from the loop would race, since the next `accept` overwrites it.
2. Detaching lets the thread release its resources on exit without the main thread having to `join`.

And one small but fatal detail: **ignore `SIGPIPE`**. Writing to a connection the peer already closed raises it, and the default action kills the whole proxy — so it must be handled or ignored.

## Stage 3 · The cache, shared safely

The cache maps a request URL to a stored response object, bounded in per-object and total size, with LRU eviction. The subtlety is that it is shared across all the threads from stage 2, so access must be synchronized — but it is **read-mostly**, so a plain mutex (which would serialize even concurrent readers) wastes the concurrency we just built.

The intended fit for the stored objects is a **reader–writer** discipline: any number of readers may copy cached bytes at once, but a writer (inserting or evicting) needs exclusive access.

``` text
   reader --.
   reader --+--  shared read (concurrent)  -->  +-------+
   reader --'                                   | cache |
                                                +-------+
   writer ------  exclusive (insert / evict) -->  locks everyone else out
```

One wrinkle: a strict LRU hit is not completely read-only, because it must update that entry's recency. The final implementation must keep that metadata update out of the shared-read section — either record a timestamp atomically (or under a short metadata lock), or take the write side for the hit. Relinking an LRU list while holding only a read lock would race.

The intended shape:

- **Lookup (reader):** acquire the read side, scan for the URL, copy out the object, and — if recency is an atomic timestamp — update it before releasing. Send only after the object has been copied out.
- **Non-atomic LRU:** treat the hit as a writer from the outset; never relink or mutate shared recency state under a read lock.
- **Store (writer):** acquire the write side, evict LRU entries until the new object fits, insert, release.
- Guard against the classic reader–writer **writer starvation**, and copy hits *out* under the lock so a concurrent eviction can't free the buffer mid-send.

## What's left

- [ ] Sequential `doit`: request parsing + header rewriting + relay
- [ ] Thread-per-connection concurrency + `SIGPIPE` handling
- [ ] Reader–writer cache with LRU eviction
- [ ] Pass the `driver.sh` basic / concurrency / cache checks

I'll write this section up properly — with the real code and its annotations — once the implementation is done.
