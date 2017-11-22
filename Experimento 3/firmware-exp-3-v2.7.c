#include "contiki.h"
#include "net/rime.h"
#include "random.h"
#include "dev/button-sensor.h"
#include "dev/leds.h"

#include <stdio.h>
#include <stdlib.h>

#define LL  0 
#define LLN 1 
#define FLL 2

static struct broadcast_conn broadcast_handler;

int classificacao_atual;
int peso_atual;

struct message {
  int w;
  int type;
};

static struct etimer timout_reset_process;
static struct etimer timout_ffl_process;

// ================================================================================================================

PROCESS(reset_process, "Reset process");
PROCESS(check_ffl_process, "check fll process");
PROCESS(broadcast_process, "Broadcast process");

AUTOSTART_PROCESSES(&broadcast_process, &reset_process, &check_ffl_process);

// ================================================================================================================

static void broadcast_recv(struct broadcast_conn *c, const rimeaddr_t *from) {
  
  timer_restart(&timout_reset_process);

  printf("%d - %d\n",peso_atual, classificacao_atual);

  struct message *m;
  m = packetbuf_dataptr(); 

  if(peso_atual < m->w)
  {    
    if(m->type == LL)
    {
      timer_restart(&timout_ffl_process);
      classificacao_atual = LLN;
    }    
  }
}

static const struct broadcast_callbacks broadcast_call = {broadcast_recv};

// ================================================================================================================

PROCESS_THREAD(broadcast_process, ev, data) {
  
  static struct etimer et;
  PROCESS_EXITHANDLER(broadcast_close(&broadcast_handler);)

  PROCESS_BEGIN();
  broadcast_open(&broadcast_handler, 129, &broadcast_call);

  peso_atual = abs(random_rand() / 100);
  classificacao_atual = LL;

  while(1) {

    /* Delay 2-4 seconds */
    etimer_set(&et, CLOCK_SECOND * 4 + random_rand() % (CLOCK_SECOND * 4));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    struct message msg;
    msg.type = classificacao_atual;
    msg.w = peso_atual;

    packetbuf_clear();
    packetbuf_copyfrom(&msg, sizeof(msg));
    broadcast_send(&broadcast_handler);
    
    printf("%d - %d\n",peso_atual, classificacao_atual);
  }

  PROCESS_END();

}

// ================================================================================================================

PROCESS_THREAD(reset_process, ev, data) {
 
  PROCESS_BEGIN();  
  while(1) {
    etimer_set(&timout_reset_process, CLOCK_SECOND * 16 + random_rand() % (CLOCK_SECOND * 16));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timout_reset_process));
    
    classificacao_atual = LL;
  }

  PROCESS_END();
}


PROCESS_THREAD(check_ffl_process, ev, data) {
  
   PROCESS_BEGIN();  
   while(1) {
     etimer_set(&timout_ffl_process, CLOCK_SECOND * 16 + random_rand() % (CLOCK_SECOND * 16));
     PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timout_ffl_process));
     
     if(classificacao_atual == LLN){
        classificacao_atual = FLL;
     }
   }
 
   PROCESS_END();
 }