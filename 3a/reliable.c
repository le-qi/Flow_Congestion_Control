
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>

#include "rlib.h"

typedef struct node node;
typedef struct send_buffer send_buffer;
typedef struct recv_buffer recv_buffer;

struct node {
	packet_t *packet;
	time_t time_sent;
	node *next;
	node *prev;
};

struct send_buffer {
	uint32_t max_size;
	uint32_t next_seqno;
	uint32_t next_acked;
	// node *last_sent;
	node *head;
	node *tail;
};

struct recv_buffer {
	uint32_t max_size;
	// uint32_t last_read;
	uint32_t next_expected;
	node *head;
	node *tail;
};

struct reliable_state {
	rel_t *next;			/* Linked list for traversing all connections */
	rel_t **prev;

	conn_t *c;			/* This is the connection object */

	/* Add your own data fields below this */
	const struct config_common *config;
	send_buffer *sbuf;
	recv_buffer *rbuf;
};

rel_t *rel_list;

void addend_send(rel_t *r, packet_t *p)
{
	node *n = xmalloc(sizeof(*n));
	memset(n, 0, sizeof(*n));
	n->packet = p;

	if (r->sbuf->head == NULL) {
		r->sbuf->head = n;
		r->sbuf->tail = n;
	}
	else {
		r->sbuf->tail->next = n;
		n->prev = r->sbuf->tail;
		r->sbuf->tail = n;
	}
}
void addend_recv(rel_t *r, packet_t *p)
{
	node *n = xmalloc(sizeof(*n));
	memset(n, 0, sizeof(*n));
	n->packet = p;

	if (r->rbuf->head == NULL) {
		r->rbuf->head = n;
		r->rbuf->tail = n;
	}
	else {
		r->rbuf->tail->next = n;
		n->prev = r->rbuf->tail;
		r->rbuf->tail = n;
	}
}
void addinorder_recv(rel_t *r, packet_t *p)
{
	node *n = xmalloc(sizeof(*n));
	memset(n, 0, sizeof(*n));
	n->packet = p;

	if (r->rbuf->head == NULL ||
			ntohl(r->rbuf->head->packet->seqno) > ntohl(n->packet->seqno)) {
		r->rbuf->head = n;
		r->rbuf->tail = n;
	}
	else {
		node *cur = r->rbuf->head;
		while (cur->next != NULL &&
				ntohl(cur->next->packet->seqno) < ntohl(n->packet->seqno)) {
			cur = cur->next;
		}
		n->next = cur->next;
		cur->next = n;
	}
}
node *remove_send(rel_t *r)
{
	if (r->sbuf->head == NULL) {
		return NULL;
	}

	node *n = r->sbuf->head;

	if (r->sbuf->head->next == NULL) {
		r->sbuf->head = NULL;
		return n;
	}

	r->sbuf->head = r->sbuf->head->next;
	r->sbuf->head->prev = NULL;
	n->next = NULL;
	return n;
}
node *remove_recv(rel_t *r)
{
	if (r->rbuf->head == NULL) {
		return NULL;
	}

	node *n = r->rbuf->head;

	if (r->rbuf->head->next == NULL) {
		r->rbuf->head = NULL;
		return n;
	}

	r->rbuf->head = r->rbuf->head->next;
	r->rbuf->head->prev = NULL;
	n->next = NULL;
	return n;
}

/* Creates a new reliable protocol session, returns NULL on failure.
 * Exactly one of c and ss should be NULL.  (ss is NULL when called
 * from rlib.c, while c is NULL when this function is called from
 * rel_demux.) */
rel_t *
rel_create (conn_t *c, const struct sockaddr_storage *ss,
		const struct config_common *cc)
{
	rel_t *r;

	r = xmalloc (sizeof (*r));
	memset (r, 0, sizeof (*r));

	if (!c) {
		c = conn_create (r, ss);
		if (!c) {
			free (r);
			return NULL;
		}
	}

	r->c = c;
	r->next = rel_list;
	r->prev = &rel_list;
	if (rel_list)
		rel_list->prev = &r->next;
	rel_list = r;

	/* Do any other initialization you need here */
	r->config = cc;

	r->sbuf = (send_buffer *) xmalloc(sizeof(send_buffer));
	r->sbuf->next_acked = 1;
	r->sbuf->next_seqno = 1;

	r->rbuf = (recv_buffer *) xmalloc(sizeof(recv_buffer));
	r->rbuf->next_expected = 1;

	return r;
}

void
rel_destroy (rel_t *r)
{
	free(r->sbuf);
	free(r->rbuf);

	if (r->next)
		r->next->prev = r->prev;
	*r->prev = r->next;
	conn_destroy (r->c);

	/* Free any other allocated memory here */
}


/* This function only gets called when the process is running as a
 * server and must handle connections from multiple clients.  You have
 * to look up the rel_t structure based on the address in the
 * sockaddr_storage passed in.  If this is a new connection (sequence
 * number 1), you will need to allocate a new conn_t using rel_create
 * ().  (Pass rel_create NULL for the conn_t, so it will know to
 * allocate a new connection.)
 */
void
rel_demux (const struct config_common *cc,
		const struct sockaddr_storage *ss,
		packet_t *pkt, size_t len)
{
}

void
rel_recvpkt (rel_t *r, packet_t *pkt, size_t n)
{
	char *op = "recv";
	uint16_t length = ntohs(pkt->len);
	uint32_t seqno = ntohl(pkt->seqno);

	if (length == 8) {

	}
	else if (length == 12) {

	}
	else {
		if (seqno == r->rbuf->next_expected) {
			r->rbuf->next_expected = r->rbuf->next_expected + 1;
			addinorder_recv(r, pkt);
			rel_output(r);
		}
		else if (seqno < r->rbuf->next_expected) {
			return;
		}
		else {
			addinorder_recv(r, pkt);
		}
	}
}


void
rel_read (rel_t *s)
{
	char buf[500];
	char *op = "send";

	memset(buf, 0, 500);

	int input;

	while (1) {
		input = conn_input(s->c, (void *) buf, 500);
		if (input == 0)
			return;

		packet_t *pkt = (packet_t *) xmalloc(sizeof(*pkt));
		memset(pkt, 0, sizeof(*pkt));

		pkt->cksum = 0; //TODO: cksum
		pkt->len = htons(input + 11);
		pkt->seqno = htonl(s->sbuf->next_seqno);
		memcpy((void *) pkt->data, buf, 500);
		s->sbuf->next_seqno = s->sbuf->next_seqno + 1;

		addend_send(s, pkt);
		print_pkt(pkt, op, input + 11);

		conn_sendpkt(s->c, pkt, input + 11);
		// s->sbuf->last_sent = s->sbuf->tail;
	}
}

void
rel_output (rel_t *r)
{
}

void
rel_timer ()
{
	/* Retransmit any packets that need to be retransmitted */
	rel_t *rel = rel_list;
	while (rel != NULL) {
		node *n = rel->sbuf->head;
		while (n != NULL) {
			conn_sendpkt(rel->c, n->packet, ntohs(n->packet->len));
			n = n->next;
		}
		rel = rel->next;
	}


}
