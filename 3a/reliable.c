
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
	uint32_t last_acked;
	node *last_sent;
	node *head;
	node *tail;
};

struct recv_buffer {
	uint32_t max_size;
	uint32_t last_read;
	uint32_t next_expected;
	node *head;
	node *tail;
};

struct reliable_state {
	rel_t *next;			/* Linked list for traversing all connections */
	rel_t **prev;

	conn_t *c;			/* This is the connection object */

	/* Add your own data fields below this */
	struct config_common *cc;
	send_buffer *sbuf;
	recv_buffer *rbuf;
};

rel_t *rel_list;

void addend_send(rel_t *r, packet_t *p)
{
	node *n = xmalloc(sizeof(*n));
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
	r->sbuf = xmalloc(sizeof(send_buffer));
	r->rbuf = xmalloc(sizeof(recv_buffer));

	return r;
}

void
rel_destroy (rel_t *r)
{
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
}


void
rel_read (rel_t *s)
{
}

void
rel_output (rel_t *r)
{
}

void
rel_timer ()
{
	/* Retransmit any packets that need to be retransmitted */

}
