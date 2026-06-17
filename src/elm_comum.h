/* ============================================================
 * elm_comum.h
 *
 * Rotinas compartilhadas entre os modos de operação da
 * aplicação unificada (elm_app) do co-processador ELM:
 *   - parser de arquivos .mif (pesos quantizados)
 *   - leitura de imagens .bin raw (784 bytes)
 *   - polling do hardware com timeout
 *   - sequência completa de inferência (reset + warmup + real)
 *   - carga dos três conjuntos de pesos no hardware
 * ============================================================ */
#ifndef ELM_COMUM_H
#define ELM_COMUM_H

#include <stdint.h>

/* ------------------------------------------------------------
 * Dimensões da rede / imagem
 * ------------------------------------------------------------ */
#define IMG_W          28
#define IMG_H          28
#define IMG_SIZE       (IMG_W * IMG_H)   /* 784 pixels             */

#define N_PESOS_W      (784 * 128)       /* W_in: 784 x 128        */
#define N_BIAS         128               /* 1 viés por neurônio    */
#define N_BETA         (10 * 128)        /* beta: 10 x 128         */

#define N_DIGITOS      10

/* ------------------------------------------------------------
 * Polling
 * ------------------------------------------------------------ */
#define POLL_TIMEOUT_MS   500
#define POLL_INTERVALO_US 10

/* ------------------------------------------------------------
 * Tempo atual em microssegundos (CLOCK_MONOTONIC)
 * ------------------------------------------------------------ */
long long tempo_us(void);

/* ------------------------------------------------------------
 * Lê um .mif de 16 bits em buf[]. Retorna nº de valores
 * lidos ou -1 em erro.
 * ------------------------------------------------------------ */
int carregar_mif_16bit(const char *caminho, uint16_t *buf, int max);

/* ------------------------------------------------------------
 * Lê um .bin raw de exatamente 784 bytes (1 byte/pixel,
 * escala de cinza, row-major). Retorna 0 ou -1.
 * ------------------------------------------------------------ */
int carregar_bin(const char *caminho, uint8_t buf[IMG_SIZE]);

/* ------------------------------------------------------------
 * Carrega W_in, bias e beta a partir dos .mif e envia ao
 * hardware via driver. Feito UMA vez na inicialização.
 * Retorna 0 em sucesso, -1 em erro.
 * ------------------------------------------------------------ */
int carregar_pesos_hardware(const char *path_W,
                            const char *path_b,
                            const char *path_beta);

/* ------------------------------------------------------------
 * Polling até DONE / ERROR / timeout.
 * Retorna:
 *    1  DONE   (*digito válido, *lat_us preenchido)
 *    0  TIMEOUT
 *   -1  ERROR reportado pelo hardware
 *   -2  MMIO não inicializada
 * ------------------------------------------------------------ */
int aguardar_resultado(int *digito, long long *lat_us);

/* ------------------------------------------------------------
 * Sequência completa de inferência sobre uma imagem 28x28:
 *   1. reset da FSM (não apaga pesos)
 *   2. inferência "fantasma" (workaround do bug da 1ª inferência)
 *   3. inferência real + polling
 * *lat_us mede apenas a inferência real.
 * Mesmos retornos de aguardar_resultado().
 * ------------------------------------------------------------ */
int inferir_digito(const uint8_t img[IMG_SIZE],
                   int *digito, long long *lat_us);

/* ------------------------------------------------------------
 * Imprime a imagem 28x28 em ASCII ('#' se pixel > 127).
 * ------------------------------------------------------------ */
void imprimir_preview_ascii(const uint8_t img[IMG_SIZE]);

#endif /* ELM_COMUM_H */
