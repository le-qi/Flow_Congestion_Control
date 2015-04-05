
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
	uint32_t next_read;
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

	int input_val;
	int eof_read;
};

rel_t *rel_list;

void addend_send(rel_t *r, packet_t *p)
{
	node *n = (node *) xmalloc(sizeof(*n));
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
	node *n = (node *) xmalloc(sizeof(*n));
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
	node *n = (node *) xmalloc(sizeof(*n));
	memset(n, 0, sizeof(*n));
	n->packet = p;

	print_pkt(p, "op", ntohs(p->len));

	if (r->rbuf->head == NULL) {
		printf("IF\n");
		r->rbuf->head = n;
		r->rbuf->tail = n;
	}
	else if (ntohl(r->rbuf->head->packet->seqno) > ntohl(n->packet->seqno)) {
		printf("ELSE IF\n");
		n->next = r->rbuf->head;
		n->prev = NULL;
		r->rbuf->head->prev = n;
		r->rbuf->head = n;
	}
	else {
		printf("ELSE\n");
		node *cur = r->rbuf->head;
		while (cur->next != NULL &&
				ntohl(cur->next->packet->seqno) < ntohl(n->packet->seqno)) {
			cur = cur->next;
		}
		n->next = cur->next;
		cur->next = n;
		n->prev = cur;

		if (n->next == NULL) {
			r->rbuf->tail = n;
		}
		else {
			n->next->prev = n;
		}
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
void print_send(rel_t *r)
{
	node *cur = r->sbuf->head;
	printf("\n-----SEND_START-----\n");
	fprintf (stderr, "max_size = %08x, next_seqno = %08x, next_acked = %08x\n",
			r->sbuf->max_size, r->sbuf->next_seqno, r->sbuf->next_acked);

	if (r->sbuf->head != NULL) {
		fprintf (stderr, "head = %08x\n",
				ntohl(r->sbuf->head->packet->seqno));
	}
	else {
		fprintf (stderr, "no head\n");
	}

	if (r->sbuf->head != NULL) {
		fprintf (stderr, "tail = %08x\n\n",
				ntohl(r->sbuf->tail->packet->seqno));
	}
	else {
		fprintf (stderr, "no tail\n\n");
	}

	const char *op = "s";
	while (cur != NULL) {
		print_pkt(cur->packet, op, ntohs(cur->packet->len));
		cur = cur->next;
	}
	printf("-----SEND_END-----\n\n");
}
void print_recv(rel_t *r)
{
	node *cur = r->rbuf->head;
	printf("\n-----RECV_START-----\n");
	fprintf (stderr, "max_size = %08x, next_read = %08x, next_expected = %08x\n",
			r->rbuf->max_size, r->rbuf->next_read, r->rbuf->next_expected);

	if (r->rbuf->head != NULL) {
		fprintf (stderr, "head = %08x\n",
				ntohl(r->rbuf->head->packet->seqno));
	}
	else {
		fprintf (stderr, "no head\n");
	}

	if (r->rbuf->head != NULL) {
		fprintf (stderr, "tail = %08x\n\n",
				ntohl(r->rbuf->tail->packet->seqno));
	}
	else {
		fprintf (stderr, "no tail\n\n");
	}

	const char *op = "r";
	while (cur != NULL) {
		print_pkt(cur->packet, op, ntohs(cur->packet->len));
		cur = cur->next;
	}
	printf("-----RECV_END-----\n\n");
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
	r->rbuf->next_read = 1;
	r->rbuf->next_expected = 1;

	r->input_val = 0;
	r->eof_read = 0;

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
	printf("ADDRESS: %p\n", pkt);

	uint16_t length = ntohs(pkt->len);
	uint32_t seqno = ntohl(pkt->seqno);
	uint32_t ackno = ntohl(pkt->ackno);

	if (length == 8) {
		if (r->sbuf->next_acked == ackno) {
			node *rmv = remove_send(r);
			free(rmv->packet);
			free(rmv);
			r->sbuf->next_acked = r->sbuf->next_acked + 1;
		}
	}
	else if (length == 12) {
		r->eof_read = 1;
		if (r->eof_read == 1 && r->input_val == -1 &&
				r->rbuf->head == NULL && r->sbuf->head == NULL) {
			rel_destroy(r);
		}
	}
	else {
		packet_t *new_pkt = (packet_t *) xmalloc(sizeof(*new_pkt));
		new_pkt->cksum = pkt->cksum;
		new_pkt->len = pkt->len;
		new_pkt->ackno = pkt->ackno;
		new_pkt->seqno = pkt->seqno;
		memcpy(new_pkt->data, pkt->data, 500);

		if (seqno == r->rbuf->next_expected) {
			addinorder_recv(r, new_pkt);
			r->rbuf->next_expected = r->rbuf->next_expected + 1;
			rel_output(r);
		}
		else if (seqno < r->rbuf->next_expected) {
		}
		else {
			addinorder_recv(r, new_pkt);
		}
	}

	print_recv(r);
}


void
rel_read (rel_t *s)
{
	char buf[500];
	char *op = "s";

	memset(buf, 0, 500);

	while (1) {
		s->input_val = conn_input(s->c, (void *) buf, 500);
		if (s->input_val == 0)
			return;

		packet_t *pkt = (packet_t *) xmalloc(sizeof(*pkt));
		memset(pkt, 0, sizeof(*pkt));

		pkt->cksum = 0; //TODO: cksum
		pkt->len = htons(s->input_val + 12);
		pkt->ackno = htonl(s->sbuf->next_acked);
		pkt->seqno = htonl(s->sbuf->next_seqno);
		memcpy((void *) pkt->data, (void *) buf, 500);
		s->sbuf->next_seqno = s->sbuf->next_seqno + 1;

		addend_send(s, pkt);
		//print_send(s);
		print_pkt(pkt, op, s->input_val + 12);
		conn_sendpkt(s->c, pkt, s->input_val + 12);
		// s->sbuf->last_sent = s->sbuf->tail;
	}

	print_recv(s);
}

void
rel_output (rel_t *r)
{
	/*
	char buf[500];
	char *op = "recv";

	memset(buf, 0, 500);

	node *cur = r->rbuf->head;
	while (cur != NULL && ntohl(cur->packet->seqno) == r->rbuf->next_read) {

		if (conn_bufspace(r->c) < (ntohs(cur->packet->len) - 12)) {
			return;
		}

		packet_t *pkt = cur->packet;
		memcpy((void *) buf, (void *) pkt->data, 500);
		conn_output(r->c, buf, 500);
		r->rbuf->next_read = r->rbuf->next_read + 1;
		print_pkt(pkt, op, ntohs(pkt->len));

		struct ack_packet *ack = (struct ack_packet *) xmalloc(sizeof(*ack));
		ack->cksum = 0;
		ack->len = htons(8);
		ack->ackno = pkt->seqno;
		conn_sendpkt(r->c, (packet_t *) ack, 8);

		cur = r->rbuf->head;
		node *n = remove_recv(r);
		//free(n->packet);
		//free(n);
	}
	 */
}

void
rel_timer ()
{
	/* Retransmit any packets that need to be retransmitted */
	/*
	rel_t *rel = rel_list;
	while (rel != NULL) {
		node *n = rel->sbuf->head;
		while (n != NULL) {
			conn_sendpkt(rel->c, n->packet, ntohs(n->packet->len));
			n = n->next;
		}
		rel = rel->next;
	}
	 */
}
