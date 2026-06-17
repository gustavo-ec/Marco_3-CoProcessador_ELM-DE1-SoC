#ifndef _ELM_H_
#define _ELM_H_

#include <stdint.h>

/* =========================================================================
 * elm.h - API estável para o co-processador ELM
 *
 * Modelo de uso:
 * if (elm_init() != 0) { ... erro ... }
 * elm_carregar_pesos_raw(...);
 * elm_carregar_bias_raw(...);
 * elm_carregar_beta_raw(...);
 * * elm_resetar_hardware(); // <-- RESET antes da inferência
 * elm_carregar_imagem(...);
 * elm_disparar_inferencia();
 * int digito;
 * while ((r = elm_obter_resultado(&digito)) == 0) usleep(10);
 * ...
 * elm_close();
 *
 * O par init/close abre e fecha /dev/mem APENAS UMA VEZ. As demais funções
 * usam o mapeamento já estabelecido (zero overhead de syscalls).
 * ========================================================================= */

/* Máscaras do registrador de status (data_out do co-processador). */
#define ELM_STATUS_BUSY   0x20
#define ELM_STATUS_DONE   0x10
#define ELM_STATUS_ERROR  0x40
#define ELM_STATUS_DIGITO 0x0F

/* ----- Códigos de retorno ----- */
#define ELM_OK           0    /* sucesso geral                            */
#define ELM_ERR_NOT_INIT -1   /* operação chamada sem elm_init() anterior */
#define ELM_ERR_MMAP     -2   /* falha em open() ou mmap() de /dev/mem    */

/* =========================================================================
 * Ciclo de vida
 * ========================================================================= */
int  elm_init(void);
void elm_close(void);
int  elm_esta_inicializado(void);

/* =========================================================================
 * Carga de dados (chamar após elm_init)
 * Todos os dados devem estar em ponto fixo Q4.12 (16 bits, signed)
 * ========================================================================= */
void elm_carregar_imagem(uint8_t *imagem);
void elm_carregar_pesos_raw(uint16_t *pesos_q412, int tamanho);
void elm_carregar_bias_raw(uint16_t *bias_q412, int tamanho);
void elm_carregar_beta_raw(uint16_t *beta_q412, int tamanho);

/* =========================================================================
 * Controle
 * ========================================================================= */
void elm_disparar_inferencia(void);
int  elm_obter_resultado(int *digito_predito);
uint32_t elm_ler_status_raw(void);

/* Limpa registradores de done/error (pulsa clr_operation) */
void elm_limpar_hardware(void);

/* Reseta o hardware por completo (pulsa o pino reset do coprocessador) */
void elm_resetar_hardware(void);

#endif /* _ELM_H_ */