/* ============================================================
 * conversor_img.h
 *
 * Conversão de imagens (PNG/JPG/BMP) para o formato de entrada
 * do co-processador ELM (28x28, 1 byte/pixel, traço claro sobre
 * fundo escuro — padrão MNIST), sem dependências externas:
 * a decodificação usa o stb_image.h (single-header, domínio
 * público), compilado junto com o projeto.
 *
 * Pipeline de conversão:
 *   1. decodifica e converte para escala de cinza;
 *   2. detecta fundo claro (média da borda) e inverte se
 *      necessário (MNIST = traço claro em fundo escuro);
 *   3. localiza a caixa delimitadora (bounding box) do dígito;
 *   4. redimensiona a caixa para caber em 20x20 (filtro de
 *      média de área, preservando proporção);
 *   5. centraliza em uma moldura 28x28 — o mesmo
 *      pré-processamento usado no dataset MNIST original.
 * ============================================================ */
#ifndef CONVERSOR_IMG_H
#define CONVERSOR_IMG_H

#include <stdint.h>
#include "elm_comum.h"   /* IMG_W, IMG_H, IMG_SIZE */

/* ------------------------------------------------------------
 * Carrega uma imagem de qualquer formato suportado:
 *   - .bin  : raw 784 bytes (lido diretamente, sem conversão)
 *   - .png / .jpg / .jpeg / .bmp : decodifica e converte
 *
 * out            : buffer 28x28 pronto para o ELM
 * foi_convertida : se não-NULL, recebe 1 quando houve
 *                  conversão (i.e., a entrada não era .bin)
 *
 * Retorna 0 em sucesso, -1 em erro (mensagem já impressa).
 * ------------------------------------------------------------ */
int carregar_imagem_qualquer(const char *caminho,
                             uint8_t out[IMG_SIZE],
                             int *foi_convertida);

/* ------------------------------------------------------------
 * Salva um buffer 28x28 como .bin raw (784 bytes).
 * Retorna 0 em sucesso, -1 em erro.
 * ------------------------------------------------------------ */
int salvar_bin(const char *caminho, const uint8_t img[IMG_SIZE]);

/* ------------------------------------------------------------
 * Monta em 'saida' o caminho de 'entrada' com a extensão
 * trocada por ".bin" (ex.: "fotos/sete.png" -> "fotos/sete.bin").
 * ------------------------------------------------------------ */
void caminho_com_extensao_bin(const char *entrada,
                              char *saida, size_t tam);

#endif /* CONVERSOR_IMG_H */
