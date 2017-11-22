// ================================================================================================================
// SIMULADOR: CONTIKI COOJA
// VERSAO: 2.7
// URL: https://sourceforge.net/projects/contiki/files/Instant%20Contiki/Instant%20Contiki%202.7/
// ================================================================================================================

#include "contiki.h"
#include "net/rime.h"
#include "random.h"
#include "dev/button-sensor.h"
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
// ESTRUTURA DE MANIPULACAO DO PROCESSO DE BROADCAST ENVIO DE MENSAGENS BROADCAST
// ================================================================================================================

static struct broadcast_conn broadcast_handler;

struct message_broadcast
{
  int function_stability;
  int type_node;
};

// ================================================================================================================

int current_rating;

int current_function_stability;

// ================================================================================================================
// TIMER PARA ROTINAS DE CLASSIFICACAO PARA LL E FLL
// ================================================================================================================

static struct etimer timout_LL_process, timout_FFL_process;

// ================================================================================================================
// ACIONA UM LED DE ACORDO COM A CLASSIFICACAO ATUAL DO DEVICE
// ================================================================================================================

void set_leds()
{
  leds_off(LEDS_ALL);
  switch (current_rating)
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
// PROCESSOS / THREADS
// ================================================================================================================

PROCESS(broadcast_process, "Broadcast process");
PROCESS(verification_LL_process, "Check kind LL process");
PROCESS(verification_FLL_process, "Check kind FLL process");

AUTOSTART_PROCESSES(&broadcast_process, &verification_LL_process, &verification_FLL_process);

// ================================================================================================================
// METODO DE RECEBIMENTO DAS MENSAGENS DE BROADCAST
// ================================================================================================================

static void response_broadcast(struct broadcast_conn *c, const rimeaddr_t *from)
{

  printf("%d - %d\n", current_function_stability, current_rating);
  set_leds();

  struct message_broadcast *m;
  m = packetbuf_dataptr();

  if (current_function_stability < m->function_stability)
  {

    if (m->type_node == LL)
    {
      current_rating = LLN;
      timer_restart(&timout_LL_process);
      timer_restart(&timout_FFL_process);
    }

    if (m->type_node == LLN && current_rating == LL)
    {
      current_rating = LLN;
      timer_restart(&timout_LL_process);
      timer_restart(&timout_FFL_process);
    }
  }
}

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
  current_rating = LL;

  while (1)
  {
    etimer_set(&et, CLOCK_SECOND * 4 + random_rand() % (CLOCK_SECOND * 4));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    struct message_broadcast msg;
    msg.type_node = current_rating;
    msg.function_stability = current_function_stability;

    packetbuf_copyfrom(&msg, sizeof(msg));
    broadcast_send(&broadcast_handler);
    printf("Enviou - %d - %d\n", current_function_stability, current_rating);
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

    if (current_rating == LLN)
      current_rating = LL;

    set_leds();
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

    if (current_rating == LLN)
      current_rating = FLL;

    set_leds();
  }

  PROCESS_END();
}
