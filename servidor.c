#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>

#define PORTA         8080
#define MAX_CLIENTES  20
#define BUFFER_SIZE   2048
#define NOME_SIZE     32

// Fila de espera
int fila_espera[MAX_CLIENTES];
int fila_count = 0;
pthread_mutex_t fila_mutex = PTHREAD_MUTEX_INITIALIZER;

// ======================= THREAD DA SALA =======================
void *sala_chat(void *arg) {
    int *clientes = (int *)arg;
    int c1 = clientes[0];
    int c2 = clientes[1];
    char buffer[BUFFER_SIZE];

    char msg_inicio[] = ">>> Você entrou em uma sala privada <<<\n";
    send(c1, msg_inicio, strlen(msg_inicio), 0);
    send(c2, msg_inicio, strlen(msg_inicio), 0);

    while (1) {
        int n = recv(c1, buffer, BUFFER_SIZE, 0);
        if (n <= 0) break;
        send(c2, buffer, n, 0);

        n = recv(c2, buffer, BUFFER_SIZE, 0);
        if (n <= 0) break;
        send(c1, buffer, n, 0);
    }

    close(c1);
    close(c2);
    free(clientes);

    pthread_exit(NULL);
}

// ======================= MAIN =======================
int main(void)
{
    int server_fd;
    struct sockaddr_in servidor_addr;
    int opt = 1;

    // Criar socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Erro ao criar socket");
        exit(EXIT_FAILURE);
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind
    memset(&servidor_addr, 0, sizeof(servidor_addr));
    servidor_addr.sin_family = AF_INET;
    servidor_addr.sin_addr.s_addr = INADDR_ANY;
    servidor_addr.sin_port = htons(PORTA);

    if (bind(server_fd, (struct sockaddr *)&servidor_addr, sizeof(servidor_addr)) == -1) {
        perror("Erro no bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Listen
    if (listen(server_fd, 10) == -1) {
        perror("Erro no listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("════════════════════════════════════════\n");
    printf("║   BATALHA DE PALAVRAS - SERVIDOR     ║\n");
    printf("║   PORTA: %d                        ║\n", PORTA);
    printf("║   AGUARDANDO JOGADORES (PARES DE 2)..║\n");
    printf("════════════════════════════════════════\n");

    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);

        int atividade = select(server_fd + 1, &read_fds, NULL, NULL, NULL);

        if (atividade < 0 && errno != EINTR) {
            perror("Erro no select");
            continue;
        }

        // Nova conexão
        if (FD_ISSET(server_fd, &read_fds)) {

            struct sockaddr_in cliente_addr;
            socklen_t cliente_len = sizeof(cliente_addr);

            int novo_fd = accept(server_fd, (struct sockaddr *)&cliente_addr, &cliente_len);
            if (novo_fd == -1) {
                perror("Erro no accept");
                continue;
            }

            printf("[+] Cliente conectado (fd=%d)\n", novo_fd);

            // Recebe nome (opcional, só pra não quebrar cliente)
            char nome[NOME_SIZE] = {0};
            recv(novo_fd, nome, NOME_SIZE - 1, 0);

            pthread_mutex_lock(&fila_mutex);

            // Verifica limite
            if (fila_count >= MAX_CLIENTES) {
                char msg[] = "Servidor cheio.\n";
                send(novo_fd, msg, strlen(msg), 0);
                close(novo_fd);
                pthread_mutex_unlock(&fila_mutex);
                continue;
            }

            fila_espera[fila_count++] = novo_fd;

            if (fila_count >= 2) {
                int *pares = malloc(2 * sizeof(int));
                pares[0] = fila_espera[0];
                pares[1] = fila_espera[1];

                // shift
                for (int i = 2; i < fila_count; i++) {
                    fila_espera[i - 2] = fila_espera[i];
                }
                fila_count -= 2;

                pthread_t tid;
                pthread_create(&tid, NULL, sala_chat, pares);
                pthread_detach(tid);

                printf("[SALA] Nova sala criada!\n");
            } else {
                char msg[] = ">>> Aguardando outro usuário...\n";
                send(novo_fd, msg, strlen(msg), 0);
            }

            pthread_mutex_unlock(&fila_mutex);
        }
    }

    close(server_fd);
    return 0;
}