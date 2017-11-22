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

#define LL 0  // LIDER LOCAL
#define LLN 1 // VIZINHO DO LIDER LOCAL
#define FLL 2 // NAO VIZINHO DO LIDER LOCAL

// ================================================================================================================
// STATUS DO DISPOTIVO
// ================================================================================================================

#define STATE_BEGIN 3    //
#define STATE_RUN 4      //
#define STATE_HAS_DATA 5 //
#define STATE_PASS_DATA 6
#define STATE_CONFIRM_PASS_DATA 7

// ================================================================================================================
// ESTRUTURA DE MANIPULACAO DO PROCESSO DE BROADCAST ENVIO DE MENSAGENS BROADCAST
// ================================================================================================================

static struct broadcast_conn broadcast_handler;
static struct unicast_conn unicast_handler;

struct message_broadcast
{
  int function_stability;
  int type_node;
};

struct message_unicast
{
  int type;
};

struct neighbor
{
  struct neighbor *next;
  rimeaddr_t addr;
};

// ================================================================================================================

#define MAX_NEIGHBORS 16

MEMB(neighbors_memb, struct neighbor, MAX_NEIGHBORS);

LIST(neighbors_list);

// ================================================================================================================

int current_classification;

int current_status = STATE_BEGIN;

int current_function_stability;

static rimeaddr_t *local_leader_address;

// ================================================================================================================
// TIMER PARA ROTINAS DE CLASSIFICACAO E REPLICACAO DE DADOS
// ================================================================================================================

static struct etimer timout_LL_process;
static struct etimer timout_FFL_process;

// ================================================================================================================
// ACIONA UM LED DE ACORDO COM A CLASSIFICACAO ATUAL DO DEVICE
// ================================================================================================================

void set_leds()
{

  leds_off(LEDS_ALL);

  switch (current_classification)
  {

  case LL:
    leds_on(LEDS_GREEN);
    break;

  case LLN:
    leds_on(LEDS_BLUE);
    break;

  case FLL:
    leds_on(LEDS_RED);
    break;
  }
}

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
  switch (current_status)
  {
  case STATE_HAS_DATA:
    return "HAS DATA";
  case STATE_BEGIN:
    return "BEGIN";
  case STATE_RUN:
    return "RUN";
  case STATE_PASS_DATA:
    return "SENDING DATA";
  default:
    return "";
  }
}

void imprimir_log()
{
  printf("%s - %d - %s\n", get_status(), current_function_stability, get_classification());
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
    &verification_FLL_process, &replication_process);

// ================================================================================================================
// METODO DE RECEBIMENTO DAS MENSAGENS DE UNICAST
// ================================================================================================================

static void response_unicast(struct unicast_conn *c, const rimeaddr_t *from)
{

  struct message_unicast *msg;
  msg = packetbuf_dataptr();

  if (msg->type == STATE_PASS_DATA)
  {
    current_status = STATE_HAS_DATA;

    msg->type = STATE_CONFIRM_PASS_DATA;
    packetbuf_copyfrom(msg, sizeof(struct message_unicast));

    unicast_send(c, from);
  }
  else
  {
    current_status = STATE_RUN;
  }

  imprimir_log();
}

// ================================================================================================================
// METODO DE RECEBIMENTO DAS MENSAGENS DE BROADCAST
// ================================================================================================================

static void response_broadcast(struct broadcast_conn *c, const rimeaddr_t *from)
{

  set_leds();
  imprimir_log();

  struct message_broadcast *m;
  m = packetbuf_dataptr();

  if (current_function_stability < m->function_stability)
  {

    if (m->type_node == LL)
    {
      current_classification = LLN;

      rimeaddr_copy(local_leader_address, from);

      timer_restart(&timout_LL_process);
      timer_restart(&timout_FFL_process);
    }

    if (m->type_node == LLN && current_classification == LL)
    {
      current_classification = LLN;
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
    list_add(neighbors_list, n);
  }
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

  current_function_stability = abs(random_rand() / 100);
  current_classification = LL;

  while (1)
  {
    etimer_set(&et, CLOCK_SECOND * 4 + random_rand() % (CLOCK_SECOND * 4));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    if (current_status != STATE_BEGIN)
    {
      struct message_broadcast msg;
      msg.type_node = current_classification;
      msg.function_stability = current_function_stability;

      packetbuf_copyfrom(&msg, sizeof(msg));
      broadcast_send(&broadcast_handler);

      set_leds();
      imprimir_log();
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
      set_leds();
      current_classification = LL;
    }
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
      set_leds();
    }
  }

  PROCESS_END();
}

// ================================================================================================================

void start_replication()
{
  struct neighbor *n;
  struct message_unicast msg;
  int randneighbor, i;

  if (list_length(neighbors_list) > 0)
  {

    randneighbor = random_rand() % list_length(neighbors_list);

    n = list_head(neighbors_list);

    for (i = 0; i < randneighbor; i++)
    {
      n = list_item_next(n);
    }

    printf("sending data to %d\n", n->addr.u8[0]);

    msg.type = STATE_PASS_DATA;
    packetbuf_copyfrom(&msg, sizeof(msg));
    unicast_send(&unicast_handler, &n->addr);

    current_status = STATE_PASS_DATA;
  }
}

// ================================================================================================================

static struct etimer timer_replication;

#define TIMER_INTERVAL_REPLICATION 15

PROCESS_THREAD(replication_process, ev, data)
{
  PROCESS_BEGIN();

  unicast_open(&unicast_handler, 146, &unicast_call);

  int count = 0;

  while (1)
  {
    etimer_set(&timer_replication, CLOCK_SECOND * TIMER_INTERVAL_REPLICATION + random_rand() % (CLOCK_SECOND * TIMER_INTERVAL_REPLICATION));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer_replication));

    if (current_status == STATE_HAS_DATA)
    {
      start_replication();
    }

    if (current_status == STATE_PASS_DATA)
    {
      if (count == 2)
      {
        current_status == STATE_HAS_DATA;
        count = 0;
      }
      else
      {
        count++;
      }
    }
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

  current_status = (atoi((char *)data) == 1) ? STATE_HAS_DATA : STATE_RUN;
  imprimir_log();

  PROCESS_END();
}
