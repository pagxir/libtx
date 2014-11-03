#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#if defined(WIN32)
#include <winsock2.h>
#else
#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#endif

#include "txall.h"

void tx_outcb_prepare(tx_aiocb *filp, tx_task_t *task, int flags)
{
	return filp->tx_fops->op_active_out(filp, task);
}

void tx_outcb_cancel(tx_aiocb *filp, void *task)
{
	return filp->tx_fops->op_cancel_out(filp, task);
}

void tx_outcb_update(tx_aiocb *filp, int len)
{
	if (len >= 0) {
		return;
	}

	if (errno == EAGAIN) {
		filp->tx_flags &= ~TX_WRITABLE;
		return;
	}

	return;
}

int tx_outcb_write(tx_aiocb *filp, const void *buf, size_t len)
{
	int n;
	tx_poll_op *ops;

	ops = filp->tx_poll->tx_ops;
	if (ops->tx_sendout == NULL) {
		n = write(filp->tx_fd, buf, len);
		tx_outcb_update(filp, n);
	} else {
		n = ops->tx_sendout(filp, buf, len);
		/* XXX */
	}

	return n;
}

int tx_outcb_xsend(tx_aiocb *filp, tx_aiobuf buf[], size_t count)
{
#ifndef WIN32
	ssize_t n = 0;
	struct iovec vec[IOV_MAX];

	TX_ASSERT(count < IOV_MAX);
	for (int i = 0; i < count; i++) {
		vec[i].iov_len = buf[i].iob_len;
		vec[i].iov_base = buf[i].iob_buf;
	}

	n = writev(filp->tx_fd, vec, count);
	tx_outcb_update(filp, n);

	return n;
#else
	return -1;
#endif
}

void tx_aincb_active(tx_aiocb *filp, tx_task_t *task)
{
	return filp->tx_fops->op_active_in(filp, task);
}

void tx_aincb_stop(tx_aiocb *filp, void *task)
{
	return filp->tx_fops->op_cancel_in(filp, task);
}

void tx_aincb_update(tx_aiocb *filp, int len)
{
	if (len >= 0) {
		return;
	}

	if (errno == EAGAIN) {
		filp->tx_flags &= ~TX_READABLE;
		return;
	}

	return;
}

static void generic_active_out(tx_aiocb *filp, tx_task_t *task)
{
	tx_poll_op *ops;

	if (tx_writable(filp)) {
		TX_CHECK(filp->tx_filterout == NULL, "tx_filterout not null");
		tx_task_active(task);
		return;
	}

	if (filp->tx_filterout != task) {
		TX_CHECK(filp->tx_filterout == NULL, "tx_filterout not null");
		filp->tx_filterout = task;
	}

	if (filp->tx_flags & TX_POLLOUT) {
		/* XXXX */
		return;
	}

	ops = filp->tx_poll->tx_ops;
	ops->tx_pollout(filp);
	return;
}

static void generic_cancel_out(tx_aiocb *filp, void *verify)
{
	if (verify == filp->tx_filterout)
		filp->tx_filterout = NULL;
	return;
}

static void generic_active_in(tx_aiocb *filp, tx_task_t *task)
{
	tx_poll_op *ops;

	if (tx_readable(filp)) {
		TX_CHECK(filp->tx_filterin == NULL, "tx_filterin not null");
		tx_task_active(task);
		return;
	}

	if (filp->tx_filterin != task) {
		TX_CHECK(filp->tx_filterin == NULL, "tx_filterout not null");
		filp->tx_filterin = task;
	}

	if (filp->tx_flags & TX_POLLIN) {
		/* XXXX */
		return;
	}

	ops = filp->tx_poll->tx_ops;
	ops->tx_pollin(filp);
	return;
}

static void generic_cancel_in(tx_aiocb *filp, void *verify)
{
	if (verify == filp->tx_filterin)
		filp->tx_filterin = NULL;
	return;
}

static tx_aiocb_op _generic_fops = {
	op_active_out: generic_active_out,
	op_cancel_out: generic_cancel_out,
       op_active_in: generic_active_in,
       op_cancel_in: generic_cancel_in
};

void tx_aiocb_init(tx_aiocb *filp, tx_poll_t *poll, int fd)
{
	tx_poll_op *ops;
	filp->tx_fd = fd;
	filp->tx_flags = 0;
	filp->tx_poll  = poll;
	filp->tx_privp = NULL;
	filp->tx_filterin = NULL;
	filp->tx_filterout = NULL;
	filp->tx_fops = &_generic_fops;

	ops = filp->tx_poll->tx_ops;
	ops->tx_attach(filp);
	return;
}

void tx_aiocb_init(tx_aiocb *filp, tx_loop_t *loop, int fd)
{
	tx_poll_t *poll = tx_poll_get(loop);
	TX_ASSERT(poll != NULL);
	tx_aiocb_init(filp, poll, fd);
	return;
}

void tx_listen_init(tx_aiocb *filp, tx_loop_t *loop, int fd)
{
	tx_poll_t *poll = tx_poll_get(loop);
	TX_ASSERT(poll != NULL);
	tx_aiocb_init(filp, poll, fd);
	filp->tx_flags |= TX_LISTEN;
	return;
}

int  tx_listen_accept(tx_aiocb *filp, struct sockaddr *sa, size_t *outlen)
{
#ifndef WIN32
	int newfd = accept(filp->tx_fd, sa, outlen);
	return newfd;
#else
	tx_poll_op *ops = filp->tx_poll->tx_ops;
	return ops->tx_accept(filp, sa, outlen);
#endif
}

void tx_aiocb_fini(tx_aiocb *filp)
{
	tx_poll_op *ops = filp->tx_poll->tx_ops;
	ops->tx_detach(filp);
	return;
}
