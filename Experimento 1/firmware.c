#include "contiki.h"

#include "lib/memb.h"
#include "lib/random.h"
#include "dev/serial-line.h"
#include "net/rime/rime.h"
#include "lib/memb.h"
#include "cc2420.h"
#include "net/packetbuf.h"
#include "contiki-conf.h"
#include "lib/list.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>

// =============================================================================================================

#define MAXIMUM_DEVICES 30
#define DEVICE_D 1
#define LATENCY_REQUIREMENT 20

enum
{

  NOT_STARTED,
  NO_TOKEN,
  CLOSED,
  WAITING_FOR_ANSWER,

  PASSING_TOKEN,
  RETURNING_TOKEN,

  ECO_SENT,
  ECO_ANSWERED,

  RETRANSMITING_REQUIREMENT_FOR_CHILDREN,
  RETRANSMITING_REQUIREMENT,
  REQUESTING_REQUIREMENT,
  SENDING_ANSWER
};

static const int latency_matrix[MAXIMUM_DEVICES][MAXIMUM_DEVICES] = {
    {0, 6, 2, 74, 35, 86, 66, 96, 16, 77, 21, 52, 41, 45, 82, 31},
    {6, 0, 3, 60, 53, 70, 86, 54, 38, 59, 63, 28, 10, 96, 95, 97},
    {2, 3, 0, 30, 8, 36, 57, 69, 65, 46, 1, 62, 94, 9, 27, 26},
    {74, 60, 30, 0, 31, 58, 37, 77, 72, 78, 52, 29, 50, 56, 47, 24},
    {35, 53, 8, 31, 0, 82, 31, 64, 76, 100, 93, 50, 27, 1, 15, 25},
    {86, 70, 36, 58, 82, 0, 40, 75, 97, 22, 94, 30, 10, 75, 65, 38},
    {66, 86, 57, 37, 31, 40, 0, 32, 95, 30, 18, 64, 39, 87, 20, 46},
    {96, 54, 69, 77, 64, 75, 32, 0, 44, 35, 27, 6, 51, 64, 41, 65},
    {16, 38, 65, 72, 76, 97, 95, 44, 0, 57, 58, 37, 48, 49, 34, 46},
    {77, 59, 46, 78, 100, 22, 30, 35, 57, 0, 2, 80, 35, 91, 59, 99},
    {21, 63, 1, 52, 93, 94, 18, 27, 58, 2, 0, 66, 98, 15, 1, 3},
    {52, 28, 62, 29, 50, 30, 64, 6, 37, 80, 66, 0, 12, 63, 8, 97},
    {41, 10, 94, 50, 27, 10, 39, 51, 48, 35, 98, 12, 0, 50, 61, 54},
    {45, 96, 9, 56, 1, 75, 87, 64, 49, 91, 15, 63, 50, 0, 23, 39},
    {82, 95, 27, 47, 15, 65, 20, 41, 34, 59, 1, 8, 61, 23, 0, 51},
    {31, 97, 26, 24, 25, 38, 46, 65, 46, 99, 3, 97, 54, 39, 51, 0}

};

// =============================================================================================================

struct route
{
  int weight;
  char way[MAXIMUM_DEVICES], kind[8];
};

struct broadcast_message
{
  double w;
  char kind[8];
  int title, latency;
  char route[MAXIMUM_DEVICES];
};

struct unicast_message
{
  double w;
  int title, index_vector_route, latency;
  char route[MAXIMUM_DEVICES], kind[8];
  struct route *solution;
};

struct vertex
{
  struct vertex *next;
  linkaddr_t addr;
};

// =============================================================================================================

static struct broadcast_conn broadcast;
static struct unicast_conn unicast;
static struct route the_best_route;

static char *kind,
    int_to_char[4],
    route_relative[MAXIMUM_DEVICES];

static double w_current;

static int number_of_vertices,
    vertex_current,
    status_token,
    status_eco,
    latency_relative,
    price_vertex,
    resouce_vertex,
    index_vector_route;

// =============================================================================================================

MEMB(route_memb, struct route, MAXIMUM_DEVICES);
MEMB(vertex_memb, struct vertex, MAXIMUM_DEVICES);

LIST(route_history);
LIST(wait_queue);

PROCESS(unicast_process, "Unicast process");
PROCESS(broadcast_process, "Broadcast process");
PROCESS(script_process, "Script");

AUTOSTART_PROCESSES(&broadcast_process, &script_process, &unicast_process);

// =============================================================================================================

void send_unicast(struct unicast_message *message, const linkaddr_t *address);

// =============================================================================================================

double calculete_w(int latency_relative)
{

  double fLatency = latency_relative / 150;
  double fPrice = price_vertex / 20;
  double fResouce = 1 / resouce_vertex;

  double w = pow(fLatency, 0.7f) *
             pow(fPrice, 0.2f) *
             pow(fResouce, 0.1f);

  return w;
}

// =============================================================================================================

static void print_wait_queue()
{

  printf("Queue: ");

  struct vertex *n;

  for (n = list_head(wait_queue); n != NULL; n = list_item_next(n))
  {
    linkaddr_t *l = &n->addr;
    printf("V = %d ", l->u8[0]);
  }

  printf("\n");
}

// =============================================================================================================

static int check_vertex_in_route(int c, char route_param[])
{

  char *string, route[50];
  int vertex;

  strcpy(route, route_param);
  string = strtok(route, "-");

  while (string != NULL)
  {

    vertex = atoi(string);

    if (vertex == c)
      return 1;

    string = strtok(NULL, "-");
  }

  return 0;
}

// =============================================================================================================

static linkaddr_t get_father_of_the_vertex_current(int vertex_current, char route_param[])
{

  static char *string, route[50];
  static int father, son;
  static linkaddr_t address;

  strcpy(route, route_param);

  string = strtok(route, "-");
  father = atoi(string);
  string = strtok(NULL, "-");

  while (string != NULL)
  {

    son = atoi(string);
    string = strtok(NULL, "-");

    if (son == vertex_current)
    {

      address.u8[0] = father;
      address.u8[1] = 0;
    }
    else
    {
      father = son;
    }
  }

  return address;
}

// =============================================================================================================

static void add_vertex_in_wait_queue(const linkaddr_t *from)
{

  struct vertex *n;

  for (n = list_head(wait_queue); n != NULL; n = list_item_next(n))
  {
    if (linkaddr_cmp(&n->addr, from))
    {
      break;
    }
  }

  if (n == NULL)
  {

    n = memb_alloc(&vertex_memb);

    if (n != NULL)
    {
      linkaddr_copy(&n->addr, from);
      list_add(wait_queue, n);
    }
  }
}

// =============================================================================================================

static void add_route_in_history(char route_param[])
{

  struct route *route;
  route = memb_alloc(&route_memb);

  if (route != NULL)
  {
    strcpy(route->way, route_param);
    list_add(route_history, route);
  }
}

// =============================================================================================================

static int search_route_in_history(char route_param[])
{

  struct route *route;

  for (route = list_head(route_history); route != NULL; route = list_item_next(route))
    if (!strcmp(route->way, route_param))
      return 1;

  return 0;
}

// =============================================================================================================

static void start_vertices(char *data)
{

  vertex_current = linkaddr_node_addr.u8[0];

  the_best_route.weight = INT_MAX;
  //the_best_route.w = 99.9f;
  index_vector_route = 0;

  char *mensagem_serial;

  mensagem_serial = strtok(data, "T");
  kind = mensagem_serial;

  mensagem_serial = strtok(NULL, "Q");
  number_of_vertices = atoi(mensagem_serial);

  mensagem_serial = strtok(NULL, "C");
  price_vertex = atoi(mensagem_serial);

  mensagem_serial = strtok(NULL, "R");
  resouce_vertex = atoi(mensagem_serial);

  status_token = (vertex_current == DEVICE_D) ? NO_TOKEN : NOT_STARTED;

  printf("Kind -> %s Price -> %d Resouce -> %d\n", kind, price_vertex, resouce_vertex);
}

// =============================================================================================================

static void send_response_to_vertex_father()
{

  static linkaddr_t address;
  static struct unicast_message message;

  address = get_father_of_the_vertex_current(vertex_current, the_best_route.way);

  message.title = SENDING_ANSWER;
  message.latency = the_best_route.weight;
  //message.w       = the_best_route.w;

  strcpy(message.kind, the_best_route.kind);
  strcpy(message.route, the_best_route.way);

  send_unicast(&message, &address);
}

// =============================================================================================================

static void closing_vertex()
{

  status_token = CLOSED;

  if (vertex_current != DEVICE_D)
  {

    static linkaddr_t address_father;
    static struct unicast_message message;

    address_father = get_father_of_the_vertex_current(vertex_current, the_best_route.way);

    message.title = RETURNING_TOKEN;

    send_unicast(&message, &address_father);
    send_response_to_vertex_father();
  }
  else
  {
    printf("\n\n\nFinished -> route -> %s; W -> %d; kind -> %s\n\n\n",
           the_best_route.way, the_best_route.weight, the_best_route.kind);
  }
}

// =============================================================================================================

void send_unicast(struct unicast_message *message, const linkaddr_t *address)
{
  packetbuf_clear();
  packetbuf_copyfrom(message, sizeof(struct unicast_message));
  unicast_send(&unicast, address);
}

static void callback_unicast_response(struct unicast_conn *c, const linkaddr_t *from)
{

  static struct unicast_message message_in, *message_out;
  static struct vertex *vertex;

  status_eco = ECO_ANSWERED;

  message_out = packetbuf_dataptr();

  switch (message_out->title)
  {

  case SENDING_ANSWER:

    if (message_out->route[0] != '\0')
    {

      if (message_out->latency < the_best_route.weight)
      {

        strcpy(the_best_route.way, message_out->route);

        the_best_route.weight = message_out->latency;

        strcpy(the_best_route.kind, message_out->kind);
      }

      if (vertex_current != DEVICE_D)
      {
        send_response_to_vertex_father();
      }
    }

    break;

  case RETURNING_TOKEN:

    if (list_length(wait_queue))
    {

      vertex = list_pop(wait_queue);

      message_in.title = PASSING_TOKEN;
      message_in.index_vector_route = index_vector_route;

      send_unicast(&message_in, &vertex->addr);
    }
    else
      closing_vertex();

    break;

  case RETRANSMITING_REQUIREMENT_FOR_CHILDREN:

    add_vertex_in_wait_queue(from);
    print_wait_queue();

    if (status_token != WAITING_FOR_ANSWER)
    {

      vertex = list_pop(wait_queue);
      status_token = WAITING_FOR_ANSWER;

      message_in.title = PASSING_TOKEN;
      send_unicast(&message_in, &vertex->addr);
    }

    break;

  case PASSING_TOKEN:

    if (status_token != CLOSED)
    {
      status_token = NO_TOKEN;
    }

    else
    {
      message_in.title = RETURNING_TOKEN;
      send_unicast(&message_in, from);
    }

    break;
  }
}

// =============================================================================================================

static void seed_broadcast()
{

  static struct broadcast_message msg;

  if (vertex_current == DEVICE_D)
  {

    msg.title = REQUESTING_REQUIREMENT;
    msg.latency = 0;
    strcat(the_best_route.way, "1-");
  }
  else
  {

    msg.title = RETRANSMITING_REQUIREMENT;
    msg.latency = the_best_route.weight;
  }

  strcpy(msg.route, the_best_route.way);
  strcpy(msg.kind, the_best_route.kind);

  printf("Seed broadcast with route -> %s\n", msg.route);

  packetbuf_clear();
  packetbuf_copyfrom(&msg, sizeof(struct broadcast_message));
  broadcast_send(&broadcast);
}

static void callback_broadcast_response(struct broadcast_conn *c, const linkaddr_t *from)
{

  static struct broadcast_message *broadcast_message;
  static struct unicast_message unicast_message;

  broadcast_message = packetbuf_dataptr();

  if (check_vertex_in_route(vertex_current, broadcast_message->route))
    return;

  printf("Received a broadcast of %d\n", from->u8[0]);

  if (broadcast_message->title == REQUESTING_REQUIREMENT)
  {
    latency_relative = latency_matrix[from->u8[0] - 1][vertex_current - 1];
  }
  else if (broadcast_message->title == RETRANSMITING_REQUIREMENT)
  {
    latency_relative = broadcast_message->latency + latency_matrix[from->u8[0] - 1][vertex_current - 1];
  }

  strcpy(route_relative, broadcast_message->route);
  sprintf(int_to_char, "%d", vertex_current);
  strcat(route_relative, int_to_char);
  strcat(route_relative, "-");

  if (latency_relative > LATENCY_REQUIREMENT)
  {
    printf("Route %s rejected! L = %d\n", route_relative, latency_relative);
    return;
  }
  else
  {
    printf("Route %s is OK! W = %d\n", route_relative, latency_relative);

    double w = calculete_w(latency_relative);

    if (the_best_route.weight > w)
    {

      the_best_route.weight = w;

      strcpy(the_best_route.kind, kind);
      strcpy(the_best_route.way, route_relative);
    }

    unicast_message.title = RETRANSMITING_REQUIREMENT_FOR_CHILDREN;

    if (search_route_in_history(route_relative))
      return;

    add_route_in_history(route_relative);
    send_unicast(&unicast_message, from);
  }
}

// =============================================================================================================

static const struct broadcast_callbacks broadcast_call = {callback_broadcast_response};
static const struct unicast_callbacks unicast_callbacks = {callback_unicast_response};

// =============================================================================================================

PROCESS_THREAD(broadcast_process, ev, data)
{

  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)
  PROCESS_BEGIN();
  broadcast_open(&broadcast, 129, &broadcast_call);

  static struct etimer et, timer;

  while (1)
  {

    etimer_set(&et, CLOCK_SECOND * 8 + random_rand() % (CLOCK_SECOND * 8));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    if (status_token == NO_TOKEN)
    {

      seed_broadcast();

      status_eco = ECO_SENT;

      etimer_set(&timer, CLOCK_SECOND * 5);
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));

      if (status_eco == ECO_SENT)
        closing_vertex();
    }
  }

  PROCESS_END();
}

// =============================================================================================================

PROCESS_THREAD(script_process, ev, data)
{

  PROCESS_BEGIN();
  broadcast_open(&broadcast, 45, &broadcast_call);

  PROCESS_YIELD_UNTIL(ev == serial_line_event_message);
  start_vertices((char *)data);

  PROCESS_END();
}

// =============================================================================================================

PROCESS_THREAD(unicast_process, ev, data)
{

  PROCESS_EXITHANDLER(unicast_close(&unicast));
  PROCESS_BEGIN();
  unicast_open(&unicast, 146, &unicast_callbacks);

  static struct etimer et;

  while (1)
  {
    etimer_set(&et, CLOCK_SECOND * 8 + random_rand() % (CLOCK_SECOND * 8));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
  }

  PROCESS_END();
}