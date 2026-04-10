#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>

#include "jogo.h"
#include "protocolo.h"

/* ------------------------------------------------------------------ */
/* Utilitários                                                          */
/* ------------------------------------------------------------------ */

void trim(char *s)
{
    if (!s) return;

    /* Remove à direita */
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r' ||
                       s[len-1] == ' '  || s[len-1] == '\t')) {
        s[--len] = '\0';
    }

    /* Remove à esquerda */
    size_t ini = 0;
    while (s[ini] && (s[ini] == ' ' || s[ini] == '\t')) ini++;
    if (ini) memmove(s, s + ini, len - ini + 1);
}

char letra_aleatoria(void)
{
    static int seed_feito = 0;
    if (!seed_feito) { srand((unsigned)time(NULL)); seed_feito = 1; }
    return (char)('A' + rand() % 26);
}

/* ------------------------------------------------------------------ */
/* Validação                                                            */
/* ------------------------------------------------------------------ */

/*
 * Verifica se um byte (ou início de sequência UTF-8) representa uma
 * letra.  Aceita ASCII a-z / A-Z e bytes de continuação de caracteres
 * UTF-8 multibyte (0x80-0xFF) – cobertura simples para acentos pt-BR.
 */
static int eh_letra(unsigned char c)
{
    return isalpha(c) || (c >= 0x80);
}

int palavra_valida(const char *palavra, char letra_inicial)
{
    if (!palavra || palavra[0] == '\0') return 0;

    /* Começa com a letra correta (case-insensitive, ASCII) */
    if (toupper((unsigned char)palavra[0]) != toupper((unsigned char)letra_inicial))
        return 0;

    /* Conta caracteres e verifica que todos são letras */
    size_t cont = 0;
    for (size_t i = 0; palavra[i]; i++) {
        if (!eh_letra((unsigned char)palavra[i])) return 0;
        /* Conta apenas bytes iniciais de caractere (não continuação UTF-8) */
        if ((palavra[i] & 0xC0) != 0x80) cont++;
    }

    return (cont >= MIN_CHARS) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Envio de mensagens — servidor → cliente                             */
/* ------------------------------------------------------------------ */

static int escrever(int fd, const char *s)
{
    size_t len = strlen(s);
    size_t enviado = 0;
    while (enviado < len) {
        ssize_t r = write(fd, s + enviado, len - enviado);
        if (r <= 0) return -1;
        enviado += (size_t)r;
    }
    return 0;
}

int enviar_msg(int fd, const char *texto)
{
    char buf[MAX_MSG];
    snprintf(buf, sizeof(buf), "%s%s%s\n", P_MSG, SEP, texto);
    return escrever(fd, buf);
}

int enviar_solicitar_nome(int fd)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%s%s\n", P_NOME_REQ, SEP);
    return escrever(fd, buf);
}

int enviar_aguarde(int fd, const char *texto)
{
    char buf[MAX_MSG];
    snprintf(buf, sizeof(buf), "%s%s%s\n", P_AGUARDE, SEP, texto);
    return escrever(fd, buf);
}

int enviar_rodada(int fd, int num, char letra, int tempo)
{
    char buf[MAX_MSG];
    snprintf(buf, sizeof(buf), "%s%s%d%s%c%s%d\n",
             P_RODADA, SEP, num, SEP, letra, SEP, tempo);
    return escrever(fd, buf);
}

int enviar_resultado(int fd, const char *texto)
{
    char buf[MAX_MSG];
    snprintf(buf, sizeof(buf), "%s%s%s\n", P_RESULTADO, SEP, texto);
    return escrever(fd, buf);
}

int enviar_placar(int fd, const char *nome1, int pts1,
                           const char *nome2, int pts2)
{
    char buf[MAX_MSG];
    snprintf(buf, sizeof(buf), "%s%s%s%s%d%s%s%s%d\n",
             P_PLACAR, SEP, nome1, SEP, pts1, SEP, nome2, SEP, pts2);
    return escrever(fd, buf);
}

int enviar_fim(int fd, const char *texto)
{
    char buf[MAX_MSG];
    snprintf(buf, sizeof(buf), "%s%s%s\n", P_FIM, SEP, texto);
    return escrever(fd, buf);
}

/* ------------------------------------------------------------------ */
/* Envio de mensagens — cliente → servidor                             */
/* ------------------------------------------------------------------ */

int enviar_nome(int fd, const char *nome)
{
    char buf[MAX_MSG];
    snprintf(buf, sizeof(buf), "%s%s%s\n", P_NOME_RESP, SEP, nome);
    return escrever(fd, buf);
}

int enviar_palavra(int fd, const char *palavra)
{
    char buf[MAX_MSG];
    snprintf(buf, sizeof(buf), "%s%s%s\n", P_PALAVRA, SEP, palavra);
    return escrever(fd, buf);
}

int enviar_timeout(int fd)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%s%s\n", P_TIMEOUT_MSG, SEP);
    return escrever(fd, buf);
}

/* ------------------------------------------------------------------ */
/* Recepção                                                             */
/* ------------------------------------------------------------------ */

ssize_t receber_linha(int fd, char *buf, size_t bufsz)
{
    size_t i = 0;
    while (i < bufsz - 1) {
        char c;
        ssize_t r = read(fd, &c, 1);
        if (r < 0) return -1;
        if (r == 0) return 0;   /* EOF */
        buf[i++] = c;
        if (c == '\n') break;
    }
    buf[i] = '\0';
    return (ssize_t)i;
}
