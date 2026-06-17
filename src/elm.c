#include "elm.h"
#include <stddef.h>

/* =========================================================================
 * elm.c - Implementação da API ELM
 * ========================================================================= */

/* ----- Funções do driver Assembly ----- */
extern void *elm_init_asm(int *out_fd);
extern void  elm_close_asm(void *base, int fd);
extern int   processar_hardware_asm(void *buffer, int tarefa,
                                    int tamanho, void *base);

/* ----- Códigos de tarefa (sincronizados com elm_driver.s) ----- */
#define TAREFA_STORE_IMG     0
#define TAREFA_STORE_WEIGHTS 1
#define TAREFA_STORE_BIAS    3
#define TAREFA_STORE_BETA    4
#define TAREFA_START         5
#define TAREFA_STATUS        6
#define TAREFA_CLEAR         7
#define TAREFA_RESET         8

/* ----- Estado interno ----- */
static void *g_mmio_base = NULL;
static int   g_mmio_fd   = -1;

/* ============================================================ */
int elm_init(void) {
    if (g_mmio_base != NULL) return ELM_OK;
    int fd = -1;
    void *base = elm_init_asm(&fd);
    if (base == NULL) return ELM_ERR_MMAP;
    g_mmio_base = base;
    g_mmio_fd   = fd;
    return ELM_OK;
}

void elm_close(void) {
    if (g_mmio_base == NULL) return;
    elm_close_asm(g_mmio_base, g_mmio_fd);
    g_mmio_base = NULL;
    g_mmio_fd   = -1;
}

int elm_esta_inicializado(void) {
    return (g_mmio_base != NULL) ? 1 : 0;
}

void elm_carregar_imagem(uint8_t *imagem) {
    if (g_mmio_base == NULL) return;
    processar_hardware_asm((void *)imagem, TAREFA_STORE_IMG, 784, g_mmio_base);
}

void elm_carregar_pesos_raw(uint16_t *pesos_q412, int tamanho) {
    if (g_mmio_base == NULL) return;
    processar_hardware_asm((void *)pesos_q412, TAREFA_STORE_WEIGHTS, tamanho, g_mmio_base);
}

void elm_carregar_bias_raw(uint16_t *bias_q412, int tamanho) {
    if (g_mmio_base == NULL) return;
    processar_hardware_asm((void *)bias_q412, TAREFA_STORE_BIAS, tamanho, g_mmio_base);
}

void elm_carregar_beta_raw(uint16_t *beta_q412, int tamanho) {
    if (g_mmio_base == NULL) return;
    processar_hardware_asm((void *)beta_q412, TAREFA_STORE_BETA, tamanho, g_mmio_base);
}

void elm_disparar_inferencia(void) {
    if (g_mmio_base == NULL) return;
    processar_hardware_asm(NULL, TAREFA_START, 0, g_mmio_base);
}

uint32_t elm_ler_status_raw(void) {
    if (g_mmio_base == NULL) return 0xFFFFFFFFu;
    return (uint32_t)processar_hardware_asm(NULL, TAREFA_STATUS, 0, g_mmio_base);
}

int elm_obter_resultado(int *digito_predito) {
    if (g_mmio_base == NULL) return -2;
    int status = processar_hardware_asm(NULL, TAREFA_STATUS, 0, g_mmio_base);

    if (status & ELM_STATUS_ERROR) return -1;
    if (status & ELM_STATUS_DONE) {
        if (digito_predito) *digito_predito = status & ELM_STATUS_DIGITO;
        return 1;
    }
    return 0;
}

void elm_limpar_hardware(void) {
    if (g_mmio_base == NULL) return;
    processar_hardware_asm(NULL, TAREFA_CLEAR, 0, g_mmio_base);
}

void elm_resetar_hardware(void) {
    if (g_mmio_base == NULL) return;
    processar_hardware_asm(NULL, TAREFA_RESET, 0, g_mmio_base);
}