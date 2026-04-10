#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#include "protocolo.h"
#include "jogo.h"

/* ------------------------------------------------------------------ */
/* Utilitários de exibição                                             */
/* ------------------------------------------------------------------ */

static void limpar_linha(void)
{
    printf("\r\033[K");   /* Move para início e limpa a linha */
    fflush(stdout);
}

static void exibir_barra_tempo(int segundos_restantes, int total)
{
    int barras = 20;
    int cheios = (segundos_restantes * barras) / total;
    printf("\r  [");
    for (int i = 0; i < barras; i++) putchar(i < cheios ? '=' : ' ');
    printf("] %2ds  ", segundos_restantes);
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* Loop principal do cliente                                           */
/* ------------------------------------------------------------------ */

static int fd_srv = -1;

static void tratar_sigint(int sig)
{
    (void)sig;
    if (fd_srv >= 0) close(fd_srv);
    printf("\n  Saindo...\n");
    exit(0);
}

int main(int argc, char *argv[])
{
    const char *ip   = "127.0.0.1";
    int         porta = PORTA_PADRAO;

    if (argc >= 2) ip    = argv[1];
    if (argc >= 3) porta = atoi(argv[2]);

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT,  tratar_sigint);

    /* Criar socket */
    fd_srv = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_srv < 0) { perror("socket"); return 1; }

    struct sockaddr_in srv_addr;
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port   = htons((uint16_t)porta);
    if (inet_pton(AF_INET, ip, &srv_addr.sin_addr) <= 0) {
        fprintf(stderr, "IP inválido: %s\n", ip);
        return 1;
    }

    printf("╔══════════════════════════════════════╗\n");
    printf("║     BATALHA DE PALAVRAS — Cliente    ║\n");
    printf("╚══════════════════════════════════════╝\n");
    printf("  Conectando a %s:%d...\n", ip, porta);

    if (connect(fd_srv, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
        perror("connect");
        return 1;
    }
    printf("  Conectado!\n\n");

    char meu_nome[MAX_NOME] = "";
    char letra_rodada = '\0';
    int  tempo_rodada  = TEMPO_RODADA;
    int  em_rodada     = 0;
    int  palavra_enviada = 0;
    time_t inicio_rodada = 0;
    int rodada_num = 0;

    while (1) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd_srv, &fds);

        /* Durante rodada: lemos teclado também */
        if (em_rodada && !palavra_enviada) {
            FD_SET(STDIN_FILENO, &fds);
        }

        struct timeval tv = {1, 0};
        int maxfd = fd_srv;

        int r = select(maxfd + 1, &fds, NULL, NULL, &tv);
        if (r < 0) {
            if (errno == EINTR) continue;
            perror("select"); break;
        }

        /* ---- Mostrar temporizador durante rodada ---- */
        if (em_rodada && !palavra_enviada) {
            int elapsed = (int)difftime(time(NULL), inicio_rodada);
            int restante = tempo_rodada - elapsed;
            if (restante < 0) restante = 0;
            exibir_barra_tempo(restante, tempo_rodada);

            if (restante == 0) {
                limpar_linha();
                printf("  Tempo esgotado! Enviando timeout...\n");
                enviar_timeout(fd_srv);
                palavra_enviada = 1;
                em_rodada = 0;
            }
        }

        if (r == 0) continue;

        /* ---- Dados do teclado ---- */
        if (em_rodada && !palavra_enviada && FD_ISSET(STDIN_FILENO, &fds)) {
            char linha[MAX_PALAVRA];
            if (fgets(linha, sizeof(linha), stdin)) {
                trim(linha);
                limpar_linha();
                if (strlen(linha) == 0) {
                    printf("  Palavra vazia ignorada. Tente novamente: ");
                    fflush(stdout);
                    continue;
                }
                enviar_palavra(fd_srv, linha);
                printf("  Enviado: \"%s\" — aguardando resultado...\n", linha);
                palavra_enviada = 1;
                em_rodada = 0;
            }
        }

        /* ---- Dados do servidor ---- */
        if (!FD_ISSET(fd_srv, &fds)) continue;

        char buf[MAX_MSG];
        ssize_t n = receber_linha(fd_srv, buf, sizeof(buf));
        if (n <= 0) {
            printf("\n  Conexão encerrada pelo servidor.\n");
            break;
        }
        trim(buf);

        /* Parsear tipo da mensagem */
        char *sep = strchr(buf, SEP_CHAR);
        char tipo[32] = "";
        char corpo[MAX_MSG] = "";

        if (sep) {
            size_t tlen = (size_t)(sep - buf);
            if (tlen >= sizeof(tipo)) tlen = sizeof(tipo) - 1;
            strncpy(tipo, buf, tlen);
            tipo[tlen] = '\0';
            strncpy(corpo, sep + 1, sizeof(corpo) - 1);
        } else {
            strncpy(tipo, buf, sizeof(tipo) - 1);
        }

        /* ---- MSG ---- */
        if (strcmp(tipo, "MSG") == 0) {
            printf("\n  %s\n\n", corpo);
        }

        /* ---- NOME (solicitação) ---- */
        else if (strcmp(tipo, "NOME") == 0) {
            printf("  Digite seu nome: ");
            fflush(stdout);

            /* Leitura bloqueante aqui — antes do loop de select */
            if (fgets(meu_nome, sizeof(meu_nome), stdin)) {
                trim(meu_nome);
                if (meu_nome[0] == '\0') strncpy(meu_nome, "Jogador", MAX_NOME - 1);
            }
            enviar_nome(fd_srv, meu_nome);
            printf("  Bem-vindo, %s!\n\n", meu_nome);
        }

        /* ---- AGUARDE ---- */
        else if (strcmp(tipo, "AGUARDE") == 0) {
            printf("  %s\n", corpo);
        }

        /* ---- RODADA ---- */
        else if (strcmp(tipo, "RODADA") == 0) {
            /* Formato: num|letra|tempo */
            int num = 0, tempo = TEMPO_RODADA;
            char letra = '?';
            sscanf(corpo, "%d|%c|%d", &num, &letra, &tempo);

            rodada_num    = num;
            letra_rodada  = letra;
            tempo_rodada  = tempo;
            em_rodada     = 1;
            palavra_enviada = 0;
            inicio_rodada = time(NULL);

            printf("\n  ╔══════════════════════════════════╗\n");
            printf("  ║        RODADA %d de %d             ║\n", num, NUM_RODADAS);
            printf("  ║  Letra: [%c]   Tempo: %d seg       ║\n", letra, tempo);
            printf("  ║  Minimo: %d caracteres            ║\n", MIN_CHARS);
            printf("  ╚══════════════════════════════════╝\n");
            printf("  Sua palavra: ");
            fflush(stdout);
        }

        /* ---- RESULTADO ---- */
        else if (strcmp(tipo, "RESULTADO") == 0) {
            printf("  Resultado: %s\n", corpo);
        }

        /* ---- PLACAR ---- */
        else if (strcmp(tipo, "PLACAR") == 0) {
            /* Formato: nome1|pts1|nome2|pts2 */
            char n1[MAX_NOME] = "", n2[MAX_NOME] = "";
            int p1 = 0, p2 = 0;
            char tmp[MAX_MSG];
            strncpy(tmp, corpo, sizeof(tmp) - 1);

            char *tok = strtok(tmp, "|");
            if (tok) { strncpy(n1, tok, MAX_NOME-1); tok = strtok(NULL, "|"); }
            if (tok) { p1 = atoi(tok);                tok = strtok(NULL, "|"); }
            if (tok) { strncpy(n2, tok, MAX_NOME-1); tok = strtok(NULL, "|"); }
            if (tok)   p2 = atoi(tok);

            printf("  ┌─────────────────────────────────┐\n");
            printf("  │  PLACAR: %-10s %d  x  %d %-10s│\n", n1, p1, p2, n2);
            printf("  └─────────────────────────────────┘\n");
        }

        /* ---- FIM ---- */
        else if (strcmp(tipo, "FIM") == 0) {
            printf("\n  ══════════════════════════════════\n");
            printf("  FIM DE JOGO: %s\n", corpo);
            printf("  ══════════════════════════════════\n\n");
            break;
        }

        /* Tipo desconhecido */
        else {
            printf("  [?] %s\n", buf);
        }

        (void)rodada_num;
        (void)letra_rodada;
    }

    close(fd_srv);
    return 0;
}
