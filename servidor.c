#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#include "protocolo.h"
#include "jogo.h"

typedef struct {
    int  fd;
    char ip[INET_ADDRSTRLEN];
    int  porta;
    char nome[MAX_NOME];
    int  pontos;
    char palavra_rodada[MAX_PALAVRA];
    int  respondeu;
    int  timeout_flag;
} Jogador;

typedef struct {
    Jogador j[2];
    int     id;
} Partida;

typedef struct {
    int  fd;
    char ip[INET_ADDRSTRLEN];
    int  porta;
} Pendente;

typedef struct {
    int  fd;
    char ip[INET_ADDRSTRLEN];
    int  porta;
    char nome[MAX_NOME];
} FilaSlot;

static pthread_mutex_t fila_mutex    = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  fila_cond     = PTHREAD_COND_INITIALIZER;
static FilaSlot        fila[MAX_CLIENTES];
static int             fila_tam      = 0;
static int             partida_count = 0;

static int receber_nome_jog(Jogador *j)
{
    if (enviar_solicitar_nome(j->fd) < 0) return -1;
    char buf[MAX_MSG];
    if (receber_linha(j->fd, buf, sizeof(buf)) <= 0) return -1;
    trim(buf);
    if (strncmp(buf, "NOME|", 5) != 0) return -1;
    strncpy(j->nome, buf + 5, MAX_NOME - 1);
    j->nome[MAX_NOME - 1] = '\0';
    trim(j->nome);
    if (j->nome[0] == '\0') strncpy(j->nome, "Anonimo", MAX_NOME - 1);
    return 0;
}

static void *thread_partida(void *arg)
{
    Partida *p  = (Partida *)arg;
    int      id = p->id;
    Jogador *ja = &p->j[0];
    Jogador *jb = &p->j[1];

    printf("[Partida #%d] Jogadores: %s vs %s\n", id, ja->nome, jb->nome);

    char anuncio[MAX_MSG];
    snprintf(anuncio, sizeof(anuncio),
             "Batalha de Palavras! %s vs %s - %d rodadas. Boa sorte!",
             ja->nome, jb->nome, NUM_RODADAS);
    enviar_msg(ja->fd, anuncio);
    enviar_msg(jb->fd, anuncio);

    ja->pontos = 0;
    jb->pontos = 0;

    for (int rodada = 1; rodada <= NUM_RODADAS; rodada++) {
        char letra = letra_aleatoria();
        printf("  [Rodada %d] Letra: %c\n", rodada, letra);

        enviar_rodada(ja->fd, rodada, letra, TEMPO_RODADA);
        enviar_rodada(jb->fd, rodada, letra, TEMPO_RODADA);

        ja->respondeu = 0; ja->timeout_flag = 0; ja->palavra_rodada[0] = '\0';
        jb->respondeu = 0; jb->timeout_flag = 0; jb->palavra_rodada[0] = '\0';

        time_t inicio = time(NULL);

        while (!(ja->respondeu && jb->respondeu)) {
            double elapsed = difftime(time(NULL), inicio);
            if (elapsed >= TEMPO_RODADA + 1) {
                if (!ja->respondeu) { ja->timeout_flag = 1; ja->respondeu = 1; }
                if (!jb->respondeu) { jb->timeout_flag = 1; jb->respondeu = 1; }
                break;
            }

            fd_set fds;
            FD_ZERO(&fds);
            int maxfd = -1;
            if (!ja->respondeu) { FD_SET(ja->fd, &fds); if (ja->fd > maxfd) maxfd = ja->fd; }
            if (!jb->respondeu) { FD_SET(jb->fd, &fds); if (jb->fd > maxfd) maxfd = jb->fd; }
            if (maxfd < 0) break;

            struct timeval tv;
            tv.tv_sec  = 1;
            tv.tv_usec = 0;
            int r = select(maxfd + 1, &fds, NULL, NULL, &tv);
            if (r < 0) break;
            if (r == 0) continue;

            Jogador *jogs[2] = {ja, jb};
            for (int k = 0; k < 2; k++) {
                Jogador *jk = jogs[k];
                if (jk->respondeu) continue;
                if (!FD_ISSET(jk->fd, &fds)) continue;

                char buf[MAX_MSG];
                ssize_t n = receber_linha(jk->fd, buf, sizeof(buf));
                if (n <= 0) { jk->timeout_flag = 1; jk->respondeu = 1; continue; }
                trim(buf);

                if (strncmp(buf, "PALAVRA|", 8) == 0) {
                    strncpy(jk->palavra_rodada, buf + 8, MAX_PALAVRA - 1);
                    jk->palavra_rodada[MAX_PALAVRA - 1] = '\0';
                    trim(jk->palavra_rodada);
                    jk->respondeu = 1;
                } else if (strncmp(buf, "TIMEOUT", 7) == 0) {
                    jk->timeout_flag = 1; jk->respondeu = 1;
                }
            }
        }

        int ok_a = 0, ok_b = 0;
        if (!ja->timeout_flag && ja->palavra_rodada[0] != '\0')
            ok_a = palavra_valida(ja->palavra_rodada, letra);
        if (!jb->timeout_flag && jb->palavra_rodada[0] != '\0')
            ok_b = palavra_valida(jb->palavra_rodada, letra);

        int empate_palavra = 0;
        if (ok_a && ok_b) {
            char tmp_a[MAX_PALAVRA], tmp_b[MAX_PALAVRA];
            strncpy(tmp_a, ja->palavra_rodada, MAX_PALAVRA - 1); tmp_a[MAX_PALAVRA-1] = '\0';
            strncpy(tmp_b, jb->palavra_rodada, MAX_PALAVRA - 1); tmp_b[MAX_PALAVRA-1] = '\0';
            for (size_t i = 0; tmp_a[i]; i++) tmp_a[i] = (char)tolower((unsigned char)tmp_a[i]);
            for (size_t i = 0; tmp_b[i]; i++) tmp_b[i] = (char)tolower((unsigned char)tmp_b[i]);
            if (strcmp(tmp_a, tmp_b) == 0) { empate_palavra = 1; ok_a = ok_b = 0; }
        }

        if (ok_a) ja->pontos++;
        if (ok_b) jb->pontos++;

        char adv_a[MAX_PALAVRA + 4], adv_b[MAX_PALAVRA + 4];
        snprintf(adv_a, sizeof(adv_a), "%s", jb->timeout_flag ? "(timeout)" : (jb->palavra_rodada[0] ? jb->palavra_rodada : "(vazio)"));
        snprintf(adv_b, sizeof(adv_b), "%s", ja->timeout_flag ? "(timeout)" : (ja->palavra_rodada[0] ? ja->palavra_rodada : "(vazio)"));

        char res_a[MAX_MSG], res_b[MAX_MSG];
        if (empate_palavra) {
            snprintf(res_a, sizeof(res_a), "Palavra \"%s\" igual a do adversario. Sem pontos.", ja->palavra_rodada);
            snprintf(res_b, sizeof(res_b), "Palavra \"%s\" igual a do adversario. Sem pontos.", jb->palavra_rodada);
        } else {
            if (ja->timeout_flag || !ja->palavra_rodada[0])
                snprintf(res_a, sizeof(res_a), "Tempo esgotado! 0 pontos. [%s: \"%s\"]", jb->nome, adv_a);
            else if (ok_a)
                snprintf(res_a, sizeof(res_a), "Palavra \"%s\" valida! +1 ponto. [%s: \"%s\"]", ja->palavra_rodada, jb->nome, adv_a);
            else
                snprintf(res_a, sizeof(res_a), "Palavra \"%s\" invalida! 0 pontos. [%s: \"%s\"]", ja->palavra_rodada, jb->nome, adv_a);

            if (jb->timeout_flag || !jb->palavra_rodada[0])
                snprintf(res_b, sizeof(res_b), "Tempo esgotado! 0 pontos. [%s: \"%s\"]", ja->nome, adv_b);
            else if (ok_b)
                snprintf(res_b, sizeof(res_b), "Palavra \"%s\" valida! +1 ponto. [%s: \"%s\"]", jb->palavra_rodada, ja->nome, adv_b);
            else
                snprintf(res_b, sizeof(res_b), "Palavra \"%s\" invalida! 0 pontos. [%s: \"%s\"]", jb->palavra_rodada, ja->nome, adv_b);
        }

        enviar_resultado(ja->fd, res_a);
        enviar_resultado(jb->fd, res_b);
        enviar_placar(ja->fd, ja->nome, ja->pontos, jb->nome, jb->pontos);
        enviar_placar(jb->fd, ja->nome, ja->pontos, jb->nome, jb->pontos);

        printf("  [Rodada %d] %s=\"%s\"(%s) | %s=\"%s\"(%s) | Placar: %d x %d\n",
               rodada,
               ja->nome, ja->timeout_flag ? "timeout" : ja->palavra_rodada,
               ok_a ? "ok" : (empate_palavra ? "empate" : "fail"),
               jb->nome, jb->timeout_flag ? "timeout" : jb->palavra_rodada,
               ok_b ? "ok" : (empate_palavra ? "empate" : "fail"),
               ja->pontos, jb->pontos);
    }

    char fim_a[MAX_MSG], fim_b[MAX_MSG];
    if (ja->pontos > jb->pontos) {
        snprintf(fim_a, sizeof(fim_a), "Voce venceu! Placar final: %s %d x %d %s", ja->nome, ja->pontos, jb->pontos, jb->nome);
        snprintf(fim_b, sizeof(fim_b), "%s venceu! Placar final: %s %d x %d %s",  ja->nome, ja->nome, ja->pontos, jb->pontos, jb->nome);
        printf("[Partida #%d] %s venceu! %d x %d\n", id, ja->nome, ja->pontos, jb->pontos);
    } else if (jb->pontos > ja->pontos) {
        snprintf(fim_b, sizeof(fim_b), "Voce venceu! Placar final: %s %d x %d %s", jb->nome, jb->pontos, ja->pontos, ja->nome);
        snprintf(fim_a, sizeof(fim_a), "%s venceu! Placar final: %s %d x %d %s",  jb->nome, ja->nome, ja->pontos, jb->pontos, jb->nome);
        printf("[Partida #%d] %s venceu! %d x %d\n", id, jb->nome, ja->pontos, jb->pontos);
    } else {
        snprintf(fim_a, sizeof(fim_a), "Empate! Placar final: %s %d x %d %s", ja->nome, ja->pontos, jb->pontos, jb->nome);
        snprintf(fim_b, sizeof(fim_b), "Empate! Placar final: %s %d x %d %s", ja->nome, ja->pontos, jb->pontos, jb->nome);
        printf("[Partida #%d] Empate! %d x %d\n", id, ja->pontos, jb->pontos);
    }

    enviar_fim(ja->fd, fim_a);
    enviar_fim(jb->fd, fim_b);

    close(ja->fd);
    close(jb->fd);
    free(p);
    return NULL;
}

static void *thread_cliente(void *arg)
{
    Pendente *pend = (Pendente *)arg;
    int  fd   = pend->fd;
    char ip[INET_ADDRSTRLEN];
    int  porta = pend->porta;
    strncpy(ip, pend->ip, INET_ADDRSTRLEN - 1);
    ip[INET_ADDRSTRLEN - 1] = '\0';
    free(pend);

    Jogador eu;
    memset(&eu, 0, sizeof(eu));
    eu.fd = fd;
    strncpy(eu.ip, ip, INET_ADDRSTRLEN - 1);
    eu.porta = porta;

    if (receber_nome_jog(&eu) < 0) {
        printf("[-] Erro ao receber nome de %s:%d\n", ip, porta);
        close(fd); return NULL;
    }
    printf("[*] Jogador '%s' identificado (%s:%d)\n", eu.nome, ip, porta);

    enviar_aguarde(fd, "Conectado! Aguardando outro jogador...");

    pthread_mutex_lock(&fila_mutex);

    fila[fila_tam].fd = fd;
    fila[fila_tam].porta = porta;
    strncpy(fila[fila_tam].ip,   ip,      INET_ADDRSTRLEN - 1);
    strncpy(fila[fila_tam].nome, eu.nome, MAX_NOME - 1);
    fila_tam++;

    printf("[*] Fila: %d jogador(es) esperando\n", fila_tam);

    if (fila_tam >= 2) {
        FilaSlot s0 = fila[0];
        FilaSlot s1 = fila[1];
        int restante = fila_tam - 2;
        if (restante > 0)
            memmove(&fila[0], &fila[2], (size_t)restante * sizeof(FilaSlot));
        fila_tam = restante;

        partida_count++;
        int pid = partida_count;

        pthread_cond_broadcast(&fila_cond);
        pthread_mutex_unlock(&fila_mutex);

        Partida *part = calloc(1, sizeof(Partida));
        if (!part) { close(s0.fd); close(s1.fd); return NULL; }
        part->id = pid;

        part->j[0].fd = s0.fd; part->j[0].porta = s0.porta;
        strncpy(part->j[0].ip,   s0.ip,   INET_ADDRSTRLEN - 1);
        strncpy(part->j[0].nome, s0.nome, MAX_NOME - 1);

        part->j[1].fd = s1.fd; part->j[1].porta = s1.porta;
        strncpy(part->j[1].ip,   s1.ip,   INET_ADDRSTRLEN - 1);
        strncpy(part->j[1].nome, s1.nome, MAX_NOME - 1);

        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&tid, &attr, thread_partida, part);
        pthread_attr_destroy(&attr);
    } else {
        pthread_cond_wait(&fila_cond, &fila_mutex);
        pthread_mutex_unlock(&fila_mutex);
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    int porta = PORTA_PADRAO;
    if (argc == 2) porta = atoi(argv[1]);

    signal(SIGPIPE, SIG_IGN);

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)porta);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(srv, BACKLOG) < 0) { perror("listen"); return 1; }

    printf("╔══════════════════════════════════════════════╗\n");
    printf("║      BATALHA DE PALAVRAS — Servidor          ║\n");
    printf("║  Porta: %-4d                                  ║\n", porta);
    printf("║  Aguardando jogadores (pares de 2)...        ║\n");
    printf("╚══════════════════════════════════════════════╝\n\n");

    while (1) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int cli_fd = accept(srv, (struct sockaddr *)&cli_addr, &cli_len);
        if (cli_fd < 0) { if (errno == EINTR) continue; perror("accept"); continue; }

        char cli_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cli_addr.sin_addr, cli_ip, sizeof(cli_ip));
        int cli_porta = ntohs(cli_addr.sin_port);

        printf("[+] Jogador conectou: %s:%d (fd=%d)\n", cli_ip, cli_porta, cli_fd);

        Pendente *pend = malloc(sizeof(Pendente));
        if (!pend) { close(cli_fd); continue; }
        pend->fd = cli_fd;
        pend->porta = cli_porta;
        strncpy(pend->ip, cli_ip, INET_ADDRSTRLEN - 1);
        pend->ip[INET_ADDRSTRLEN - 1] = '\0';

        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&tid, &attr, thread_cliente, pend);
        pthread_attr_destroy(&attr);
    }

    close(srv);
    return 0;
}
