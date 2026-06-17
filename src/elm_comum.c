/* ============================================================
 * elm_comum.c
 *
 * Implementação das rotinas compartilhadas da aplicação
 * unificada do co-processador ELM. Consolida o código que
 * antes estava duplicado entre benchmark.c, golden_test.c
 * e mouse_paint.c.
 * ============================================================ */

/* Feature-test macros: devem vir antes de qualquer #include.
 * Garantem usleep/strcasecmp tanto na glibc 2.15 da DE1-SoC
 * quanto em toolchains modernas. */
#define _BSD_SOURCE
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

#include "elm.h"
#include "elm_comum.h"

/* ============================================================
 * Tempo em microssegundos
 * ============================================================ */
long long tempo_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000LL;
}

/* ============================================================
 * Parser de .mif (Memory Initialization File - Altera)
 *
 * Formato:
 *   DEPTH = N;  WIDTH = 16;  ...
 *   CONTENT
 *   BEGIN
 *   0 : FD3A;
 *   ...
 *   END;
 * ============================================================ */
int carregar_mif_16bit(const char *caminho, uint16_t *buf, int max)
{
    FILE *f = fopen(caminho, "r");
    if (!f) {
        fprintf(stderr, "[elm_app] Erro abrindo '%s': %s\n",
                caminho, strerror(errno));
        return -1;
    }

    char linha[256];
    int dentro = 0;
    int n = 0;

    while (fgets(linha, sizeof(linha), f)) {
        if (!dentro) {
            if (strstr(linha, "BEGIN")) dentro = 1;
            continue;
        }
        if (strstr(linha, "END")) break;

        int endereco;
        unsigned int valor;
        if (sscanf(linha, " %d : %x", &endereco, &valor) == 2) {
            if (endereco < 0 || endereco >= max) {
                fprintf(stderr,
                        "[elm_app] MIF '%s': endereço %d fora do limite %d\n",
                        caminho, endereco, max);
                fclose(f);
                return -1;
            }
            buf[endereco] = (uint16_t)(valor & 0xFFFF);
            n++;
        }
    }

    fclose(f);
    return n;
}

/* ============================================================
 * Lê .bin raw (784 bytes exatos)
 * ============================================================ */
int carregar_bin(const char *caminho, uint8_t buf[IMG_SIZE])
{
    FILE *f = fopen(caminho, "rb");
    if (!f) {
        fprintf(stderr, "[elm_app] Erro abrindo '%s': %s\n",
                caminho, strerror(errno));
        return -1;
    }

    size_t lidos = fread(buf, 1, IMG_SIZE, f);
    fclose(f);

    if (lidos != IMG_SIZE) {
        fprintf(stderr,
                "[elm_app] '%s' tem %zu bytes, esperado %d.\n",
                caminho, lidos, IMG_SIZE);
        return -1;
    }
    return 0;
}

/* ============================================================
 * Carga dos pesos no hardware (feita UMA vez)
 * ============================================================ */
int carregar_pesos_hardware(const char *path_W,
                            const char *path_b,
                            const char *path_beta)
{
    /* Heap: W_in é grande demais para a pilha */
    uint16_t *pesos = calloc(N_PESOS_W, sizeof(uint16_t));
    uint16_t *bias  = calloc(N_BIAS,    sizeof(uint16_t));
    uint16_t *beta  = calloc(N_BETA,    sizeof(uint16_t));

    if (!pesos || !bias || !beta) {
        fprintf(stderr, "[elm_app] Falha de alocação de memória.\n");
        free(pesos); free(bias); free(beta);
        return -1;
    }

    int ok = -1;

    printf("[elm_app] Carregando W_in   '%s'...\n", path_W);
    int n_w = carregar_mif_16bit(path_W, pesos, N_PESOS_W);
    if (n_w <= 0) goto fim;
    printf("[elm_app]   -> %d valores lidos.\n", n_w);

    printf("[elm_app] Carregando bias   '%s'...\n", path_b);
    int n_b = carregar_mif_16bit(path_b, bias, N_BIAS);
    if (n_b <= 0) goto fim;
    printf("[elm_app]   -> %d valores lidos.\n", n_b);

    printf("[elm_app] Carregando beta   '%s'...\n", path_beta);
    int n_beta = carregar_mif_16bit(path_beta, beta, N_BETA);
    if (n_beta <= 0) goto fim;
    printf("[elm_app]   -> %d valores lidos.\n", n_beta);

    printf("[elm_app] Enviando W_in ao hardware...\n");
    elm_carregar_pesos_raw(pesos, N_PESOS_W);
    printf("[elm_app] Enviando bias ao hardware...\n");
    elm_carregar_bias_raw(bias, N_BIAS);
    printf("[elm_app] Enviando beta ao hardware...\n");
    elm_carregar_beta_raw(beta, N_BETA);
    printf("[elm_app] Pesos carregados com sucesso.\n");

    ok = 0;

fim:
    free(pesos);
    free(bias);
    free(beta);
    return ok;
}

/* ============================================================
 * Polling até DONE / ERROR / timeout
 * ============================================================ */
int aguardar_resultado(int *digito, long long *lat_us)
{
    long long t0      = tempo_us();
    long long timeout = (long long)POLL_TIMEOUT_MS * 1000LL;

    while (1) {
        int r = elm_obter_resultado(digito);

        if (r != 0) {                 /* DONE(1), ERROR(-1), NOT_INIT(-2) */
            *lat_us = tempo_us() - t0;
            return r;
        }
        if (tempo_us() - t0 > timeout) {
            *lat_us = tempo_us() - t0;
            return 0;                 /* TIMEOUT */
        }
        usleep(POLL_INTERVALO_US);
    }
}

/* ============================================================
 * Inferência completa: clear + reset + warmup fantasma + real
 *
 * Sequência alinhada à API (API.md) e ao teste de estabilidade
 * T12 do golden_test, que validou 100% de determinismo:
 *   - elm_limpar_hardware(): pulsa o clear (bit 1 de
 *     pio_signals), limpando os flags DONE/ERROR pendentes;
 *   - elm_resetar_hardware(): pulsa o reset (bit 2),
 *     reiniciando a FSM (NÃO apaga os pesos na RAM).
 *
 * O hardware tem o comportamento conhecido de a primeira
 * inferência após reset retornar DONE instantâneo com lixo.
 * Por isso disparamos uma inferência descartada antes da real
 * (mesmo padrão validado no mouse_paint e no golden_test).
 * ============================================================ */
int inferir_digito(const uint8_t img[IMG_SIZE],
                   int *digito, long long *lat_us)
{
    if (!elm_esta_inicializado()) {
        *lat_us = 0;
        return -2;   /* mesmo código de elm_obter_resultado p/ MMIO não inicializada */
    }

    /* 1. Limpa flags DONE/ERROR e reseta a FSM */
    elm_limpar_hardware();
    elm_resetar_hardware();

    /* 2. Inferência "fantasma" (resultado descartado) */
    elm_carregar_imagem((uint8_t *)img);
    elm_disparar_inferencia();
    usleep(20000);   /* 20 ms para o hardware "engasgar" em paz */

    /* 3. Inferência real */
    elm_carregar_imagem((uint8_t *)img);
    elm_disparar_inferencia();

    return aguardar_resultado(digito, lat_us);
}

/* ============================================================
 * Preview ASCII da imagem 28x28
 * ============================================================ */
void imprimir_preview_ascii(const uint8_t img[IMG_SIZE])
{
    printf("--- Visão do co-processador ELM (28x28) ---\n");
    for (int r = 0; r < IMG_H; r++) {
        for (int c = 0; c < IMG_W; c++)
            printf("%c", img[r * IMG_W + c] > 127 ? '#' : '.');
        printf("\n");
    }
    printf("--------------------------------------------\n");
}
