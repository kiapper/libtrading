#include "libtrading/proto/fast_session.h"

#include <stdlib.h>

struct fast_session *fast_session_new(int sockfd)
{
	struct fast_session *self = calloc(1, sizeof *self);

	if (!self)
		return NULL;

	self->rx_buffer		= buffer_new(FAST_RECV_BUFFER_SIZE);
	if (!self->rx_buffer) {
		fast_session_free(self);
		return NULL;
	}

	self->tx_message_buffer		= buffer_new(FAST_TX_BUFFER_SIZE);
	if (!self->tx_message_buffer) {
		fast_session_free(self);
		return NULL;
	}

	self->tx_pmap_buffer		= buffer_new(FAST_TX_BUFFER_SIZE);
	if (!self->tx_pmap_buffer) {
		fast_session_free(self);
		return NULL;
	}

	self->rx_messages	= fast_message_new(FAST_TEMPLATE_MAX_NUMBER);
	if (!self->rx_messages) {
		fast_session_free(self);
		return NULL;
	}

	self->sockfd		= sockfd;
	self->last_tid		= 0;
	self->nr_messages	= 0;

	return self;
}

void fast_session_free(struct fast_session *self)
{
	if (!self)
		return;

	fast_message_free(self->rx_messages, FAST_TEMPLATE_MAX_NUMBER);
	buffer_delete(self->tx_message_buffer);
	buffer_delete(self->tx_pmap_buffer);
	buffer_delete(self->rx_buffer);
	free(self);
}

static inline bool fast_session_buffer_full(struct fast_session *session)
{
	return buffer_remaining(session->rx_buffer) <= FAST_MESSAGE_MAX_SIZE;
}

struct fast_message *fast_session_recv(struct fast_session *self, int flags)
{
	struct fast_message *msgs = self->rx_messages;
	struct buffer *buffer = self->rx_buffer;
	u64 last_tid = self->last_tid;
	struct fast_message *msg;
	const char *start_prev;
	size_t size;
	ssize_t nr;
	long shift;

	start_prev = buffer_start(buffer);

	msg = fast_message_decode(msgs, buffer, last_tid);
	if (msg) {
		self->last_tid = msg->tid;
		return msg;
	}

	shift = start_prev - buffer_start(buffer);

	buffer_advance(buffer, shift);

	if (fast_session_buffer_full(self))
		buffer_compact(buffer);

	size = buffer_remaining(buffer);
	if (size > FAST_MESSAGE_MAX_SIZE) {
		size -= FAST_MESSAGE_MAX_SIZE;

		nr = buffer_nread(buffer, self->sockfd, size);
		if (nr < 0)
			return NULL;
	}

	if (!buffer_size(buffer))
		return NULL;

	msg = fast_message_decode(msgs, buffer, last_tid);
	if (msg)
		self->last_tid = msg->tid;

	return msg;
}

int fast_session_send(struct fast_session *self, struct fast_message *msg, int flags)
{
	msg->pmap_buf = self->tx_pmap_buffer;
	buffer_reset(msg->pmap_buf);
	msg->msg_buf = self->tx_message_buffer;
	buffer_reset(msg->msg_buf);

	return fast_message_send(msg, self->sockfd, flags);
}

void fast_session_reset(struct fast_session *self)
{
	int i;

	for (i = 0; i < self->nr_messages; i++)
		fast_message_reset(self->rx_messages + i);
}
