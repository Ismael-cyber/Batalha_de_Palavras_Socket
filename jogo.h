#ifndef JOGO_H
#define JOGO_H

#include <stddef.h>
#include "protocolo.h"

/* ------------------------------------------------------------------ */
/* Validação                                                           */
/* ------------------------------------------------------------------ */

/*
 * Verifica se a palavra é válida segundo as regras:
 *  - começa com a letra indicada (case-insensitive)
 *  - tem pelo menos MIN_CHARS caracteres
 *  - contém apenas letras (a-z, A-Z, e letras acentuadas UTF-8)
 * Retorna 1 se válida, 0 se inválida.
 */
int palavra_valida(const char *palavra, char letra_inicial);

/* Gera uma letra aleatória de A a Z. */
char letra_aleatoria(void);

/* Remove espaços e '\n' do início e fim da string (in-place). */
void trim(char *s);

/* ------------------------------------------------------------------ */
/* Comunicação formatada (servidor → cliente)                          */
/* ------------------------------------------------------------------ */

/* Envia: MSG|texto\n */
int enviar_msg(int fd, const char *texto);

/* Envia: NOME|\n  (solicita nome) */
int enviar_solicitar_nome(int fd);

/* Envia: AGUARDE|texto\n */
int enviar_aguarde(int fd, const char *texto);

/* Envia: RODADA|num|letra|tempo\n */
int enviar_rodada(int fd, int num, char letra, int tempo);

/* Envia: RESULTADO|texto\n */
int enviar_resultado(int fd, const char *texto);

/* Envia: PLACAR|nome1|pts1|nome2|pts2\n */
int enviar_placar(int fd, const char *nome1, int pts1,
                           const char *nome2, int pts2);

/* Envia: FIM|texto\n */
int enviar_fim(int fd, const char *texto);

/* ------------------------------------------------------------------ */
/* Comunicação formatada (cliente → servidor)                          */
/* ------------------------------------------------------------------ */

/* Envia: NOME|nome\n */
int enviar_nome(int fd, const char *nome);

/* Envia: PALAVRA|palavra\n */
int enviar_palavra(int fd, const char *palavra);

/* Envia: TIMEOUT|\n */
int enviar_timeout(int fd);

/* ------------------------------------------------------------------ */
/* Recepção genérica                                                    */
/* ------------------------------------------------------------------ */

/*
 * Lê uma linha (até '\n') do fd, armazena em buf (tamanho bufsz).
 * Retorna o número de bytes lidos, 0 em EOF, -1 em erro.
 */
ssize_t receber_linha(int fd, char *buf, size_t bufsz);

#endif /* JOGO_H */
