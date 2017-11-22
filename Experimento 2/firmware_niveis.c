#include "contiki.h"
#include "lib/random.h"
#include "net/rime/rime.h"
#include "dev/serial-line.h"

#include <stdio.h>

#define LIMITE_FRONTEIRA_SINAL -80
#define LATENCIA_PARAM          150

enum { INICIO, FINALIZADO, PRONTO };

struct broadcast_message {
  uint8_t interacao;
};

static struct broadcast_conn broadcast;

static int status = INICIO;
static uint8_t interacao_atual = 0;

static const int latencia[10] = {0, 10, 50, 100, 150, 200, 250, 300, 350, 400};

PROCESS(broadcast_process, "Broadcast process");
AUTOSTART_PROCESSES(&broadcast_process);


// ========================================================================================
// Função de broadcast
// ========================================================================================

static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from) {
  
  uint16_t nivel_sinal = packetbuf_attr(PACKETBUF_ATTR_RSSI);

   if(nivel_sinal > LIMITE_FRONTEIRA_SINAL){
    status = FINALIZADO;
  
  } else {
    
    if(status == INICIO) {
       
       struct broadcast_message *m;
       m = packetbuf_dataptr();      
       interacao_atual = m->interacao;

       printf("Recebei broadcast de %d sinal -> %d interacao -> %d\n", from->u8[0], nivel_sinal, m->interacao);

       status = PRONTO;

       if (LATENCIA_PARAM <= latencia[interacao_atual]) {
          status = FINALIZADO;
          printf("Finalizou no %d nivel\n", interacao_atual - 1);
       }           
    }
  }
}

static const struct broadcast_callbacks broadcast_call = {broadcast_recv};


// ========================================================================================
// Theads 
// ========================================================================================

PROCESS_THREAD(broadcast_process, ev, data) {
  
  static struct etimer et;
  struct broadcast_message msg;

  PROCESS_EXITHANDLER(broadcast_close(&broadcast));
  PROCESS_BEGIN();
  
  broadcast_open(&broadcast, 129, &broadcast_call);
  
  while(1) { 

    etimer_set(&et, status == INICIO ? CLOCK_SECOND * 16 + random_rand() % (CLOCK_SECOND * 16) : CLOCK_SECOND);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    switch (status){

      case INICIO:

        if(linkaddr_node_addr.u8[0] == 1) {

          printf("Interacao atual -> %u\n", interacao_atual);
          msg.interacao = interacao_atual + 1;

          packetbuf_copyfrom(&msg, sizeof(struct broadcast_message));
          broadcast_send(&broadcast);
          printf("Enviou broadcast....\n");
          
          status = FINALIZADO;
        }
      break;

      case PRONTO:

        printf("Interacao atual -> %u\n", interacao_atual);
        msg.interacao = interacao_atual + 1;
        
        packetbuf_copyfrom(&msg, sizeof(struct broadcast_message));
        broadcast_send(&broadcast);
        printf("Enviou broadcast....\n");
        
        status = FINALIZADO;
        
      break;
    }
  }

  PROCESS_END();
}