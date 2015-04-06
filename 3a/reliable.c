
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
	int timeout;
};

rel_t *rel_list;

void addend_send(rel_t *r, packet_t *p)
{
	node *n = (node *) xmalloc(sizeof(*n));
	memset(n, 0, sizeof(*n));
	n->packet = p;
	time(&n->time_sent);

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

	if (r->rbuf->head == NULL) {
		r->rbuf->head = n;
		r->rbuf->tail = n;
	}
	else if (ntohl(r->rbuf->head->packet->seqno) == ntohl(n->packet->seqno)) {
		free(p);
		free(n);
	}
	else if (ntohl(r->rbuf->head->packet->seqno) > ntohl(n->packet->seqno)) {
		n->next = r->rbuf->head;
		n->prev = NULL;
		r->rbuf->head->prev = n;
		r->rbuf->head = n;
	}
	else {
		node *cur = r->rbuf->head;
		while (cur->next != NULL &&
				ntohl(cur->next->packet->seqno) <= ntohl(n->packet->seqno)) {
			if (ntohl(cur->next->packet->seqno) == ntohl(n->packet->seqno)) {
				free(p);
				free(n);
				return;
			}
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
		printf("data = %s\n", cur->packet->data);
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
		printf("data = %s\n", cur->packet->data);
		cur = cur->next;
	}
	printf("-----RECV_END-----\n\n");
}
void test_packets(rel_t *s)
{

	packet_t *pkt0 = (packet_t *) xmalloc(sizeof(*pkt0));
	memset(pkt0, 0, sizeof(*pkt0));
	char *zero = "a";
	pkt0->cksum = 0;
	pkt0->len = htons(13);
	pkt0->ackno = htonl(s->sbuf->next_acked);
	pkt0->seqno = htonl(3);
	strcpy(pkt0->data, zero);
	addend_send(s, pkt0);
	conn_sendpkt(s->c, pkt0, 13);

	packet_t *pkt1 = (packet_t *) xmalloc(sizeof(*pkt1));
	memset(pkt1, 0, sizeof(*pkt1));
	char *one = "bb";
	pkt1->cksum = 0;
	pkt1->len = htons(14);
	pkt1->ackno = htonl(s->sbuf->next_acked);
	pkt1->seqno = htonl(3);
	strcpy((void *) pkt1->data, (void *) one);
	addend_send(s, pkt1);
	conn_sendpkt(s->c, pkt1, 14);

	packet_t *pkt2 = (packet_t *) xmalloc(sizeof(*pkt2));
	memset(pkt2, 0, sizeof(*pkt2));
	char *two = "ccc";
	pkt2->cksum = 0;
	pkt2->len = htons(15);
	pkt2->ackno = htonl(s->sbuf->next_acked);
	pkt2->seqno = htonl(4);
	strcpy((void *) pkt2->data, (void *) two);
	addend_send(s, pkt2);
	conn_sendpkt(s->c, pkt2, 15);

	packet_t *pkt3 = (packet_t *) xmalloc(sizeof(*pkt3));
	memset(pkt3, 0, sizeof(*pkt3));
	char *three = "dddd";
	pkt3->cksum = 0;
	pkt3->len = htons(16);
	pkt3->ackno = htonl(s->sbuf->next_acked);
	pkt3->seqno = htonl(1);
	strcpy((void *) pkt3->data, (void *) three);
	addend_send(s, pkt3);
	conn_sendpkt(s->c, pkt3, 16);

	packet_t *pkt4 = (packet_t *) xmalloc(sizeof(*pkt4));
	memset(pkt4, 0, sizeof(*pkt4));
	char *four = "eeeee";
	pkt4->cksum = 0;
	pkt4->len = htons(17);
	pkt4->ackno = htonl(s->sbuf->next_acked);
	pkt4->seqno = htonl(2);
	strcpy((void *) pkt4->data, (void *) four);
	addend_send(s, pkt4);
	conn_sendpkt(s->c, pkt4, 17);

	// print_send(s);
}
uint32_t size_recv(rel_t *r)
{
	uint32_t size = 0;
	node *cur = r->rbuf->head;
	while (cur != NULL) {
		size = size + 1;
		cur = cur->next;
	}
	return size;
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

	r->sbuf = (send_buffer *) xmalloc(sizeof(send_buffer));
	r->sbuf->next_acked = 1;
	r->sbuf->next_seqno = 1;
	r->sbuf->max_size = cc->window;
	cc->timeout;

	r->rbuf = (recv_buffer *) xmalloc(sizeof(recv_buffer));
	r->rbuf->next_read = 1;
	r->rbuf->next_expected = 1;
	r->rbuf->max_size = cc->window;

	r->input_val = 0;
	r->eof_read = 0;
	r->timeout = cc->timeout;

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
	uint16_t length = ntohs(pkt->len);
	uint32_t seqno = ntohl(pkt->seqno);
	uint32_t ackno = ntohl(pkt->ackno);

	if (length == 8) {
		//print_pkt(pkt, "r", ntohs(pkt->len));
		//printf("CKSUM: %04x\n", cksum(&pkt->len, 6));
		if (cksum(&pkt->len, 6) != pkt->cksum)
			return;
		if (r->sbuf->next_acked < ackno) {
			node *rmv = remove_send(r);
			free(rmv->packet);
			free(rmv);
			r->sbuf->next_acked = r->sbuf->next_acked + 1;
		}
	}
	else if (length == 12) {
		if (cksum(&pkt->len, 10) != pkt->cksum)
			return;
		r->eof_read = 1;
		if (r->eof_read == 1 && r->input_val == -1 &&
				r->rbuf->head == NULL && r->sbuf->head == NULL) {
			rel_destroy(r);
		}
	}
	else {
		//print_pkt(pkt, "r", ntohs(pkt->len));
		//printf("CKSUM: %04x\n", cksum(&pkt->len, ntohs(pkt->len) - 2));
		if (cksum(&pkt->len, ntohs(pkt->len) - 2) != pkt->cksum)
			return;
		packet_t *new_pkt = (packet_t *) xmalloc(sizeof(*new_pkt));
		//memset(new_pkt, 0, sizeof(*new_pkt));
		new_pkt->cksum = pkt->cksum;
		new_pkt->len = pkt->len;
		new_pkt->ackno = pkt->ackno;
		new_pkt->seqno = pkt->seqno;
		strcpy(new_pkt->data, pkt->data);

		if (seqno == r->rbuf->next_expected) {
			addinorder_recv(r, new_pkt);
			node *cur = r->rbuf->head;
			while (cur != NULL && ntohl(cur->packet->seqno) < r->rbuf->next_expected) {
				cur = cur->next;
			}
			while (cur != NULL && ntohl(cur->packet->seqno) == r->rbuf->next_expected) {
				r->rbuf->next_expected = r->rbuf->next_expected + 1;
				cur = cur->next;
			}
			rel_output(r);
		}
		else if (seqno < r->rbuf->next_expected) {
			struct ack_packet *ack = (struct ack_packet *) xmalloc(sizeof(*ack));
			ack->len = htons(8);
			ack->ackno = htonl(r->rbuf->next_expected);
			ack->cksum = cksum(&ack->len, 6);
			conn_sendpkt(r->c, (packet_t *) ack, 8);
			free(new_pkt);
			free(ack);
		}
		else {
			if (size_recv(r) < r->rbuf->max_size - 1) {
				addinorder_recv(r, new_pkt);
			}
		}
	}

	// print_send(r);
	// print_recv(r);

}

void
rel_read (rel_t *s)
{

	char buf[500];
	while (1) {

		memset(buf, 0, 500);

		if (s->sbuf->next_seqno - s->sbuf->next_acked >= s->sbuf->max_size)
			return;

		s->input_val = conn_input(s->c, (void *) buf, 500);
		if (s->input_val == 0)
			return;

		packet_t *pkt = (packet_t *) xmalloc(sizeof(*pkt));
		memset(pkt, 0, sizeof(*pkt));

		pkt->len = htons(s->input_val + 12);
		pkt->ackno = htonl(s->sbuf->next_acked);
		pkt->seqno = htonl(s->sbuf->next_seqno);
		strcpy((void *) pkt->data, (void *) buf);
		s->sbuf->next_seqno = s->sbuf->next_seqno + 1;
		pkt->cksum = cksum(&pkt->len, ntohs(pkt->len) - 2);

		//print_pkt(pkt, "s", ntohs(pkt->len));
		//printf("%s\n", pkt->data);
		addend_send(s, pkt);
		conn_sendpkt(s->c, pkt, s->input_val + 12);
	}

	//test_packets(s);
}

void
rel_output (rel_t *r)
{
	char buf[500];
	node *cur = r->rbuf->head;

	while (cur != NULL && ntohl(cur->packet->seqno) == r->rbuf->next_read) {

		memset(buf, 0, 500);
		if (conn_bufspace(r->c) < (ntohs(cur->packet->len) - 12)) {
			return;
		}

		packet_t *pkt = cur->packet;
		strcpy((void *) buf, (void *) pkt->data);
		//print_recv(r);
		conn_output(r->c, (void *) buf, ntohs(pkt->len) - 12);
		r->rbuf->next_read = r->rbuf->next_read + 1;

		struct ack_packet *ack = (struct ack_packet *) xmalloc(sizeof(*ack));
		ack->len = htons(8);
		ack->ackno = htonl(r->rbuf->next_expected);
		ack->cksum = cksum(&ack->len, 6);
		conn_sendpkt(r->c, (packet_t *) ack, 8);
		free(ack);

		node *n = remove_recv(r);
		free(n->packet);
		free(n);

		cur = r->rbuf->head;
	}
}

void
rel_timer ()
{
	/* Retransmit any packets that need to be retransmitted */
	rel_t *rel = rel_list;
	while (rel != NULL) {
		node *n = rel->sbuf->head;
		while (n != NULL) {
			time(&n->time_sent);
			conn_sendpkt(rel->c, n->packet, ntohs(n->packet->len));
			n = n->next;
		}
		rel = rel->next;
	}
}
