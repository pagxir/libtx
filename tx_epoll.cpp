#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef __linux__
#include <fcntl.h>
#include <sys/epoll.h>
#endif

#include "txall.h"

#define MAX_EVENTS 10

#ifdef __linux__

typedef struct tx_epoll_t {
	int epoll_fd;
	int epoll_refcnt;
	tx_poll_t epoll_task;
} tx_epoll_t;

static void tx_epoll_pollout(tx_file_t *filp);
static void tx_epoll_attach(tx_file_t *filp);
static void tx_epoll_pollin(tx_file_t *filp);
static void tx_epoll_detach(tx_file_t *filp);

static tx_poll_op _epoll_ops = {
	.tx_pollout = tx_epoll_pollout,
	.tx_attach = tx_epoll_attach,
	.tx_pollin = tx_epoll_pollin,
	.tx_detach = tx_epoll_detach
};

static void tx_epoll_pollout(tx_file_t *filp)
{
	int error;
	int flags, tflag;
	tx_epoll_t *epoll;
	epoll_event event = {0};
	epoll = container_of(filp->tx_poll, tx_epoll_t, epoll_task);
	TX_ASSERT(filp->tx_poll->tx_ops == &_epoll_ops);

	if ((filp->tx_flags & TX_POLLOUT) == 0x0) {
		flags = TX_ATTACHED | TX_DETACHED;
		TX_ASSERT((filp->tx_flags & flags) == TX_ATTACHED);

		tflag = (filp->tx_flags & TX_POLLIN);
		event.events   = (tflag? EPOLLIN: 0) | EPOLLOUT | EPOLLONESHOT;
		event.data.ptr = filp;

		error = epoll_ctl(epoll->epoll_fd, EPOLL_CTL_MOD, filp->tx_fd, &event);
		epoll->epoll_refcnt += (error == 0 && tflag == 0);
		filp->tx_flags |= (error == 0? TX_POLLOUT: 0);
		TX_CHECK(error == 0, "epoll ctl pollout failure");
	}

	if (epoll->epoll_refcnt > 0) {
		tx_loop_t *loop = tx_loop_get(&epoll->epoll_task.tx_task);
		if (!loop->tx_holder) 
			loop->tx_holder = epoll;
	}

	return;
}

static void tx_epoll_attach(tx_file_t *filp)
{
	int error;
	int flags, tflag;
	tx_epoll_t *epoll;
	epoll_event event = {0};
	epoll = container_of(filp->tx_poll, tx_epoll_t, epoll_task);
	TX_ASSERT(filp->tx_poll->tx_ops == &_epoll_ops);

	flags = TX_ATTACHED| TX_DETACHED;
	tflag = (filp->tx_flags & flags);
	if (tflag == 0 || tflag == flags) {
		event.data.ptr = filp;
		error = epoll_ctl(epoll->epoll_fd, EPOLL_CTL_ADD, filp->tx_fd, &event);
		filp->tx_flags &= ~(error == 0? TX_DETACHED: 0);
		filp->tx_flags |= (error == 0? TX_ATTACHED: 0);
		TX_CHECK(error == 0, "epoll ctl attach failure");
	}

	flags = fcntl(filp->tx_fd, F_GETFL);
	fcntl(filp->tx_fd, F_SETFL, flags | O_NONBLOCK);
	return;
}

static void tx_epoll_pollin(tx_file_t *filp)
{
	int error;
	int flags, tflag;
	tx_epoll_t *epoll;
	epoll_event event = {0};
	epoll = container_of(filp->tx_poll, tx_epoll_t, epoll_task);
	TX_ASSERT(filp->tx_poll->tx_ops == &_epoll_ops);

	if ((filp->tx_flags & TX_POLLIN) == 0x0) {
		flags = TX_ATTACHED | TX_DETACHED;
		TX_ASSERT((filp->tx_flags & flags) == TX_ATTACHED);

		tflag = (filp->tx_flags & TX_POLLOUT);
		event.events   = (tflag? EPOLLOUT: 0) | EPOLLIN | EPOLLONESHOT;
		event.data.ptr = filp;

		error = epoll_ctl(epoll->epoll_fd, EPOLL_CTL_MOD, filp->tx_fd, &event);
		epoll->epoll_refcnt += (error == 0 && tflag == 0);
		filp->tx_flags |= (error == 0? TX_POLLIN: 0);
		TX_CHECK(error == 0, "epoll ctl pollin failure");
	}

	if (epoll->epoll_refcnt > 0) {
		tx_loop_t *loop = tx_loop_get(&epoll->epoll_task.tx_task);
		if (!loop->tx_holder) 
			loop->tx_holder = epoll;
	}

	return;
}

static void tx_epoll_detach(tx_file_t *filp)
{
	int error;
	int flags;
	tx_epoll_t *epoll;
	epoll_event event = {0};
	epoll = container_of(filp->tx_poll, tx_epoll_t, epoll_task);
	TX_ASSERT(filp->tx_poll->tx_ops == &_epoll_ops);

	flags = TX_ATTACHED| TX_DETACHED;
	if ((filp->tx_flags & flags) == TX_ATTACHED) {
		event.data.ptr = filp;
		error = epoll_ctl(epoll->epoll_fd, EPOLL_CTL_DEL, filp->tx_fd, &event);
		filp->tx_flags |= (error == 0? TX_ATTACHED: 0);
		TX_CHECK(error == 0, "epoll ctl detach failure");
	}

	flags = TX_POLLIN| TX_POLLOUT;
	if (flags & filp->tx_flags) {
		tx_loop_t *loop = tx_loop_get(&epoll->epoll_task.tx_task);
		epoll->epoll_refcnt --;
		if (loop->tx_holder == epoll &&
				epoll->epoll_refcnt == 0)
			loop->tx_holder = NULL;
	}

	return;
}

static void tx_epoll_pollit(tx_epoll_t *epoll, tx_file_t *filp)
{
	int error;
	int flin, flout;
	int flags = filp->tx_flags;
	epoll_event event = {0};

	flin = (flags & TX_POLLIN);
	flout = (flags & TX_POLLOUT);

	event.events = (flin? EPOLLOUT: 0) | (flout? EPOLLIN: 0) | EPOLLONESHOT;
	event.data.ptr = filp;
	error = epoll_ctl(epoll->epoll_fd, EPOLL_CTL_DEL, filp->tx_fd, &event);
	epoll->epoll_refcnt += (error == 0? 1: 0);
	filp->tx_flags |= (error == 0? flout: 0);
	filp->tx_flags |= (error == 0? flin: 0);
	TX_CHECK(error == 0, "epoll ctl detach failure");

	return;
}

static void tx_epoll_polling(void *up)
{
	int i;
	int nfds;
	int timeout;
	tx_loop_t *loop;
	tx_epoll_t *poll;
	struct epoll_event events[MAX_EVENTS];

	poll = (tx_epoll_t *)up;
	loop = tx_loop_get(&poll->epoll_task.tx_task);
	timeout = tx_loop_timeout(loop, poll);

	nfds = epoll_wait(poll->epoll_fd, events, MAX_EVENTS, timeout? 10: 0);
	TX_PANIC(nfds != -1, "epoll_wait");

	for (i = 0; i < nfds; ++i) {
		int flags = events[i].events;
		tx_file_t *filp = (tx_file_t *)events[i].data.ptr;

		filp->tx_flags &= ~(TX_POLLIN| TX_POLLOUT);
		poll->epoll_refcnt--; 

		TX_PRINT(TXL_DEBUG, "nr epoll_pwait %d %x", poll->epoll_refcnt, flags);
		if (flags & (EPOLLHUP| EPOLLERR)) {
			filp->tx_flags |= (TX_READABLE| TX_WRITABLE);
			tx_task_active(filp->tx_filterout);
			filp->tx_filterout = NULL;
			tx_task_active(filp->tx_filterin);
			filp->tx_filterin = NULL;
			continue;
		}

		if (flags & EPOLLOUT) {
			tx_task_active(filp->tx_filterout);
			filp->tx_flags |= TX_WRITABLE;
			filp->tx_filterout = NULL;
		}

		if (flags & EPOLLIN) {
			tx_task_active(filp->tx_filterin);
			filp->tx_flags |= TX_READABLE;
			filp->tx_filterin = NULL;
		}

		flags = TX_POLLIN| TX_POLLOUT;
		if (filp->tx_flags & flags) {
			tx_epoll_pollit(poll, filp);
			continue;
		}
	}

	if (loop->tx_holder == poll &&
		poll->epoll_refcnt == 0)
		loop->tx_holder = NULL;

	tx_poll_active(&poll->epoll_task);
	return;
}
#endif

tx_poll_t * tx_epoll_init(tx_loop_t *loop)
{
	int fd = -1;
	tx_task_t *np = 0;
	tx_task_q *taskq = &loop->tx_taskq;

#ifdef __linux__
	if (loop->tx_poller != NULL &&
		loop->tx_poller->tx_ops == &_epoll_ops) {
		TX_PRINT(TXL_ERROR, "completion port aready created");
		return loop->tx_poller;
	}

	 TAILQ_FOREACH(np, taskq, entries)
		if (np->tx_call == tx_epoll_polling)
			return container_of(np, tx_poll_t, tx_task);

	tx_epoll_t *poll = (tx_epoll_t *)malloc(sizeof(tx_epoll_t));
	TX_CHECK(poll != NULL, "create epoll failure");

	fd = epoll_create(10);
	TX_CHECK(fd != -1, "create epoll failure");

	if (poll != NULL && fd != -1) {
		tx_poll_init(&poll->epoll_task, loop, tx_epoll_polling, poll);
		tx_poll_active(&poll->epoll_task);
		poll->epoll_task.tx_ops = &_epoll_ops;
		loop->tx_poller = &poll->epoll_task;
		poll->epoll_refcnt = 0;
		poll->epoll_fd = fd;
		return &poll->epoll_task;
	}

	free(poll);
	close(fd);
#endif

	taskq = taskq;
	fd = fd; //avoid warning
	np = np;
	return NULL;
}

