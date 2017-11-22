#include "contiki.h"
#include "lib/list.h"
#include "lib/memb.h"
#include "lib/random.h"
#include "net/rime.h"

#include <stdio.h>

struct broadcast_message {
  uint8_t seqno;
};

struct unicast_message {
  uint8_t type;
};

enum {
  UNICAST_TYPE_PING,
  UNICAST_TYPE_PONG
};

struct neighbor {
  struct neighbor *next;
  rimeaddr_t addr;
};

#define MAX_NEIGHBORS 16

MEMB(neighbors_memb, struct neighbor, MAX_NEIGHBORS);
LIST(neighbors_list);

static struct broadcast_conn broadcast;
static struct unicast_conn unicast;

PROCESS(broadcast_process, "Broadcast process");
PROCESS(unicast_process, "Unicast process");

AUTOSTART_PROCESSES(&broadcast_process, &unicast_process);

static void broadcast_recv(struct broadcast_conn *c, const rimeaddr_t *from) {

  struct neighbor *n;
  struct broadcast_message *m;

  m = packetbuf_dataptr();

  for(n = list_head(neighbors_list); n != NULL; n = list_item_next(n)) {

    if(rimeaddr_cmp(&n->addr, from)) {
      break;
    }
  }


  if(n == NULL) {

    n = memb_alloc(&neighbors_memb);

    if(n == NULL) {
      return;
    }

    rimeaddr_copy(&n->addr, from);

    list_add(neighbors_list, n);
  }

  printf("Recebei broadcast de %d.%d\n", from->u8[0], from->u8[1]);

  printf("Meus vizinhos -> [ ");
  for(n = list_head(neighbors_list); n != NULL; n = list_item_next(n)) {
    printf("%d.%d ", n->addr.u8[0], n->addr.u8[1]);    
  }
  printf("]\n");

}


static const struct broadcast_callbacks broadcast_call = {broadcast_recv};

static void recv_uc(struct unicast_conn *c, const rimeaddr_t *from) {

  struct unicast_message *msg;

  msg = packetbuf_dataptr();

  if(msg->type == UNICAST_TYPE_PING) {

    printf("unicast ping received from %d.%d\n", from->u8[0], from->u8[1]);
    msg->type = UNICAST_TYPE_PONG;
    packetbuf_copyfrom(msg, sizeof(struct unicast_message));
    unicast_send(c, from);
  }
}

static const struct unicast_callbacks unicast_callbacks = {recv_uc};

PROCESS_THREAD(broadcast_process, ev, data){

  static struct etimer et;
  static uint8_t seqno;

  struct broadcast_message msg;

  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)

  PROCESS_BEGIN();

  broadcast_open(&broadcast, 129, &broadcast_call);

  while(1) {

    etimer_set(&et, CLOCK_SECOND * 16 + random_rand() % (CLOCK_SECOND * 16));

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    msg.seqno = seqno;
    packetbuf_copyfrom(&msg, sizeof(struct broadcast_message));
    broadcast_send(&broadcast);
    seqno++;
  }

  PROCESS_END();
}

PROCESS_THREAD(unicast_process, ev, data) {

  PROCESS_EXITHANDLER(unicast_close(&unicast);)

  PROCESS_BEGIN();

  unicast_open(&unicast, 146, &unicast_callbacks);

  while(1) {
    static struct etimer et;
    struct unicast_message msg;
    struct neighbor *n;
    int randneighbor, i;
    
    etimer_set(&et, CLOCK_SECOND * 8 + random_rand() % (CLOCK_SECOND * 8));
    
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    if(list_length(neighbors_list) > 0) {
      randneighbor = random_rand() % list_length(neighbors_list);
      n = list_head(neighbors_list);
      for(i = 0; i < randneighbor; i++) {
        n = list_item_next(n);
      }
      printf("sending unicast to %d.%d\n", n->addr.u8[0], n->addr.u8[1]);

      msg.type = UNICAST_TYPE_PING;
      packetbuf_copyfrom(&msg, sizeof(msg));
      unicast_send(&unicast, &n->addr);
    }
  }

  PROCESS_END();
}