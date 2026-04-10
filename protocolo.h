#ifndef PROTOCOLO_H
#define PROTOCOLO_H

/* Configurações de rede */
#define PORTA_PADRAO    7070
#define MAX_CLIENTES    64
#define BACKLOG         10

/* Configurações do jogo */
#define NUM_RODADAS     5
#define TEMPO_RODADA    10
#define MIN_CHARS       5
#define MAX_NOME        64
#define MAX_PALAVRA     128
#define MAX_MSG         512

/* Prefixos servidor → cliente */
#define P_MSG           "MSG"
#define P_NOME_REQ      "NOME"
#define P_AGUARDE       "AGUARDE"
#define P_RODADA        "RODADA"
#define P_RESULTADO     "RESULTADO"
#define P_PLACAR        "PLACAR"
#define P_FIM           "FIM"

/* Prefixos cliente → servidor */
#define P_NOME_RESP     "NOME"
#define P_PALAVRA       "PALAVRA"
#define P_TIMEOUT_MSG   "TIMEOUT"

/* Separador de campos */
#define SEP             "|"
#define SEP_CHAR        '|'

#endif /* PROTOCOLO_H */
