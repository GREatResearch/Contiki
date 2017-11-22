// ================================================================================================================
// SIMULADOR: CONTIKI COOJA
// VERSAO: 2.7
// URL: https://sourceforge.net/projects/contiki/files/Instant%20Contiki/Instant%20Contiki%202.7/
// ================================================================================================================

#include "contiki.h"
#include "net/rime.h"
#include "random.h"
#include "dev/button-sensor.h"
#include "dev/serial-line.h"
#include "dev/leds.h"

#include <stdio.h>
#include <stdlib.h>

// ================================================================================================================
// TIPOS DE DISPOTIVOS
// ================================================================================================================

enum
{
  LL,  // LIDER LOCAL
  LLN, // VIZINHO DO LIDER LOCAL
  FLL, // NAO VIZINHO DO LIDER LOCAL

  RUN,
  BEGIN,
  HAS_DATA,
  WAITING,

  SENDING_DATA,
  CONFIRM_DATA_OK,
  SENDING_STATUS,
  GET_STATUS
};

// ================================================================================================================

static int current_classification;
static int current_state = BEGIN;

static int current_value_stability;
static int current_value_attractiveness;

static int authorized_replication = 0;
static int numeroTentativas = 0;

static rimeaddr_t last_neighbor_address;
// static rimeaddr_t local_leader_address;

// ================================================================================================================
// ESTRUTURA DE MANIPULACAO DO PROCESSO DE BROADCAST ENVIO DE MENSAGENS BROADCAST
// ================================================================================================================

static struct broadcast_conn broadcast_handler;
static struct unicast_conn unicast_handler;

struct message_broadcast
{
  int value_attractiveness;
  int value_stability;
  int type_node;
};

struct message_unicast
{
  int type;
  int value;
};

struct neighbor
{
  struct neighbor *next;
  rimeaddr_t addr;
  int value_attractiveness;
  int value_stability;
};

static struct neighbor local_leader;

// ================================================================================================================

#define MAX_NEIGHBORS 16

MEMB(neighbors_memb, struct neighbor, MAX_NEIGHBORS);

LIST(neighbors_list);

// ================================================================================================================
// TIMER PARA ROTINAS DE CLASSIFICACAO E REPLICACAO DE DADOS
// ================================================================================================================

static struct etimer timout_LL_process;
static struct etimer timout_FFL_process;

// ================================================================================================================
// FUNCAO GERAL PARA VISUALIZACAO DE LOG
// ================================================================================================================

const char *get_classification()
{
  switch (current_classification)
  {
  case LL:
    return "LL";
  case LLN:
    return "LLN";
  case FLL:
    return "FLL";
  default:
    return "";
  }
}

const char *get_status()
{
  switch (current_state)
  {
  case HAS_DATA:
    return "HAS";
  case RUN:
    return "RUN";
  case WAITING:
    return "WAITING";
  default:
    return "";
  }
}

void show_log()
{
  if (current_state == HAS_DATA)
  {
    leds_on(LEDS_ALL);
  }

  else if (current_state == WAITING)
  {
    leds_off(LEDS_ALL);
    leds_on(LEDS_RED);
  }
  else
  {
    leds_off(LEDS_ALL);
  }

  printf("%s - %d - %s\n", get_status(), current_value_stability, get_classification());
}

// ================================================================================================================
// PROCESSOS / THREADS
// ================================================================================================================

PROCESS(broadcast_process, "");

PROCESS(verification_LL_process, "");
PROCESS(verification_FLL_process, "");

PROCESS(script_process, "");

PROCESS(replication_process, "");

AUTOSTART_PROCESSES(
    &broadcast_process,
    &script_process,
    &verification_LL_process,
    &verification_FLL_process,
    &replication_process);

// ================================================================================================================
// METODO DE RECEBIMENTO DAS MENSAGENS DE UNICAST
// ================================================================================================================

static void response_unicast(struct unicast_conn *c, const rimeaddr_t *from)
{
  printf("DATA unicast from %d\n", from->u8[0]);

  struct message_unicast *msg;
  msg = packetbuf_dataptr();

  switch (msg->type)
  {

  case SENDING_DATA:

    current_state = HAS_DATA;
    authorized_replication = 0;

    rimeaddr_copy(&last_neighbor_address, from);

    msg->type = CONFIRM_DATA_OK;
    packetbuf_copyfrom(msg, sizeof(struct message_unicast));
    unicast_send(c, from);
    break;

  case CONFIRM_DATA_OK:
    current_state = RUN;
    break;

  case GET_STATUS:

    msg->type = SENDING_STATUS;
    msg->value = current_state;

    packetbuf_copyfrom(msg, sizeof(struct message_unicast));
    unicast_send(c, from);
    break;

  case SENDING_STATUS:

    if (msg->value == RUN && current_state == HAS_DATA)
    {
      authorized_replication = 1;
    }

    else if (msg->value == WAITING && current_state == HAS_DATA)
    {
      msg->type = CONFIRM_DATA_OK;
      packetbuf_copyfrom(msg, sizeof(struct message_unicast));
      unicast_send(c, from);
    }

    else if (msg->value == HAS_DATA && current_state == WAITING)
    {
      current_state = RUN;
      authorized_replication = 0;
    }

    else if (msg->value == RUN && current_state == WAITING)
    {
      current_state = HAS_DATA;
      authorized_replication = 1;
    }

    break;

  default:
    printf("DATA ERRO REPLICATION!!!\n");
    break;
  }

  show_log();
}

// ================================================================================================================
// METODO DE RECEBIMENTO DAS MENSAGENS DE BROADCAST
// ================================================================================================================

static void response_broadcast(struct broadcast_conn *c, const rimeaddr_t *from)
{

  struct message_broadcast *m;
  m = packetbuf_dataptr();

  if (current_value_stability < m->value_stability)
  {

    if (m->type_node == LL)
    {
      current_classification = LLN;
      rimeaddr_copy(&local_leader.addr, from);

      timer_restart(&timout_LL_process);
      timer_restart(&timout_FFL_process);
    }

    if (m->type_node == LLN && current_classification == LL)
    {
      current_classification = LLN;
      rimeaddr_copy(&local_leader.addr, from);

      timer_restart(&timout_LL_process);
      timer_restart(&timout_FFL_process);
    }
  }

  struct neighbor *n;

  // verifica se ja existe um vizinho com mesmo ip
  for (n = list_head(neighbors_list); n != NULL; n = list_item_next(n))
  {
    if (rimeaddr_cmp(&n->addr, from))
    {
      break;
    }
  }

  // como nao encontrou, adiciona-se um novo a lista
  if (n == NULL)
  {
    n = memb_alloc(&neighbors_memb);
    if (n == NULL)
    {
      return;
    }

    rimeaddr_copy(&n->addr, from);
    n->value_attractiveness = m->value_attractiveness;
    n->value_stability = m->value_stability;

    list_add(neighbors_list, n);
  }

  show_log();
}

// ================================================================================================================

static const struct unicast_callbacks unicast_call = {response_unicast};
static const struct broadcast_callbacks broadcast_call = {response_broadcast};

// ================================================================================================================
// PROCESSO DE ENVIO DE MENSAGEM DE BROADCAST
// ================================================================================================================

PROCESS_THREAD(broadcast_process, ev, data)
{
  static struct etimer et;
  PROCESS_EXITHANDLER(broadcast_close(&broadcast_handler);)

  PROCESS_BEGIN();

  broadcast_open(&broadcast_handler, 129, &broadcast_call);

  while (1)
  {
    etimer_set(&et, CLOCK_SECOND * 4 + random_rand() % (CLOCK_SECOND * 4));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    if (current_state != BEGIN)
    {
      struct message_broadcast msg;
      msg.type_node = current_classification;
      msg.value_stability = current_value_stability;
      msg.value_attractiveness = current_value_attractiveness;

      packetbuf_copyfrom(&msg, sizeof(msg));
      broadcast_send(&broadcast_handler);

      show_log();
    }
  }
  PROCESS_END();
}

// ================================================================================================================
// PROCESSO DE CLASSIFICAÇÃO PARA LL
// ================================================================================================================

PROCESS_THREAD(verification_LL_process, ev, data)
{
  PROCESS_BEGIN();

  while (1)
  {

    etimer_set(&timout_LL_process, CLOCK_SECOND * 16 + random_rand() % (CLOCK_SECOND * 16));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timout_LL_process));

    if (current_classification == LLN)
    {
      current_classification = LL;
    }

    show_log();
  }

  PROCESS_END();
}

// ================================================================================================================
// PROCESSO DE CLASSIFICAÇÃO PARA FLL
// ================================================================================================================

PROCESS_THREAD(verification_FLL_process, ev, data)
{

  PROCESS_BEGIN();

  while (1)
  {

    etimer_set(&timout_FFL_process, CLOCK_SECOND * 16 + random_rand() % (CLOCK_SECOND * 16));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timout_FFL_process));

    if (current_classification == LLN)
    {
      current_classification = FLL;
    }

    show_log();
  }

  PROCESS_END();
}

// ================================================================================================================

void rouletteWheelSelection(struct neighbor *n)
{
  int sum_fi = 0;
  int sum = 0;
  int r = abs(random_rand() / 100);

  for (n = list_head(neighbors_list); n != NULL; n = list_item_next(n))
  {
    sum_fi += n->value_attractiveness;
  }

  for (n = list_head(neighbors_list); n != NULL; n = list_item_next(n))
  {
    sum += (n->value_attractiveness / sum_fi);

    if (sum > r)
    {
      break;
    }
  }

  if (n == NULL)
  {
    n = list_head(neighbors_list);
  }
}

PROCESS_THREAD(replication_process, ev, data)
{

  PROCESS_EXITHANDLER(unicast_close(&unicast_handler));
  PROCESS_BEGIN();

  unicast_open(&unicast_handler, 146, &unicast_call);

  static struct etimer timer_replication;
  static int timer_process = 15, randneighbor, i, sum_fi, sum, r;
  static struct neighbor *n;
  struct message_unicast msg;

  while (1)
  {

    etimer_set(&timer_replication, CLOCK_SECOND * timer_process + random_rand() % (CLOCK_SECOND * timer_process));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer_replication));

    timer_process = 2;

    if (list_length(neighbors_list) > 0)
    {

      if (current_state == HAS_DATA)
      {

        if (authorized_replication == 1)
        {

          switch (current_classification)
          {

          case FLL:

            sum_fi = 0;
            sum = 0;
            r = abs(random_rand() / 100);

            for (n = list_head(neighbors_list); n != NULL; n = list_item_next(n))
            {
              sum_fi += n->value_attractiveness;
            }

            for (n = list_head(neighbors_list); n != NULL; n = list_item_next(n))
            {
              sum += (n->value_attractiveness / sum_fi);

              if (sum > r)
              {
                break;
              }
            }

            if (n == NULL)
            {
              n = list_head(neighbors_list);
            }

            msg.type = SENDING_DATA;
            current_state = WAITING;
            printf("DATA -> %d\n", n->addr.u8[0]);

            packetbuf_copyfrom(&msg, sizeof(msg));
            unicast_send(&unicast_handler, &n->addr);
            break;

          case LLN:

            n = &local_leader;

            msg.type = SENDING_DATA;
            current_state = WAITING;
            printf("DATA -> %d\n", n->addr.u8[0]);

            packetbuf_copyfrom(&msg, sizeof(msg));
            unicast_send(&unicast_handler, &n->addr);

            break;

          case LL:
            break;

          default:
            break;
          }
        }

        else
        {
          msg.type = GET_STATUS;
          printf("GET STATUS DATA-> %d\n", last_neighbor_address.u8[0]);

          packetbuf_copyfrom(&msg, sizeof(msg));
          unicast_send(&unicast_handler, &last_neighbor_address);
        }
      }

      else if (current_state == WAITING)
      {

        if (numeroTentativas == 10)
        {
          current_state = HAS_DATA;
          numeroTentativas = 0;
          printf("DATA TIMEOUT\n");
        }

        else
        {

          printf("RESEND DATA x%d -> %d.%d\n", numeroTentativas, n->addr.u8[0], n->addr.u8[1]);
          numeroTentativas++;

          msg.type = GET_STATUS;

          packetbuf_copyfrom(&msg, sizeof(msg));
          unicast_send(&unicast_handler, &n->addr);
        }
      }
    }
    show_log();
  }

  PROCESS_END();
}

// ================================================================================================================
// SCRIPT DE INICIALIZACAO DOS DEVICES
// ================================================================================================================

PROCESS_THREAD(script_process, ev, data)
{
  PROCESS_BEGIN();
  PROCESS_YIELD_UNTIL(ev == serial_line_event_message);

  current_value_stability = abs(random_rand() / 100) + 50;
  current_value_attractiveness = abs(random_rand() / 100);

  current_classification = LL;

  if (atoi((char *)data) == 1)
  {
    current_state = HAS_DATA;
    authorized_replication = 1;
  }
  else
  {
    current_state = RUN;
    authorized_replication = 0;
  }

  show_log();

  PROCESS_END();
}