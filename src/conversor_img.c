/* ============================================================
 * conversor_img.c
 *
 * Implementação da conversão PNG/JPG/BMP -> 28x28 (MNIST-like)
 * usando stb_image.h. Veja conversor_img.h para o pipeline.
 * ============================================================ */

#define _BSD_SOURCE
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */
#include <errno.h>

/* --- stb_image: só os formatos necessários, sem HDR ---------- */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_NO_LINEAR
#define STBI_NO_HDR
/* Silencia warnings do header externo sob -Wall -Wextra */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "stb_image.h"
#pragma GCC diagnostic pop

#include "conversor_img.h"

/* Lado da área útil do dígito dentro da moldura 28x28
 * (padrão do pré-processamento do MNIST original). */
#define DIGITO_LADO 20

/* Limiar para considerar um pixel como "tinta" ao buscar
 * a bounding box (após a normalização p/ fundo escuro). */
#define LIMIAR_TINTA 50

/* ============================================================
 * Verifica extensão (case-insensitive)
 * ============================================================ */
static int tem_extensao(const char *caminho, const char *ext)
{
    size_t lc = strlen(caminho), le = strlen(ext);
    if (lc < le) return 0;
    return strcasecmp(caminho + lc - le, ext) == 0;
}

void caminho_com_extensao_bin(const char *entrada,
                              char *saida, size_t tam)
{
    snprintf(saida, tam, "%s", entrada);

    char *ponto  = strrchr(saida, '.');
    char *barra  = strrchr(saida, '/');
    if (ponto && (!barra || ponto > barra))
        *ponto = '\0';                       /* remove a extensão */

    size_t len = strlen(saida);
    snprintf(saida + len, tam - len, ".bin");
}

/* ============================================================
 * Redimensiona um retângulo (x0,y0,rw,rh) da imagem fonte para
 * dw x dh usando média de área (box filter). Adequado para
 * REDUÇÃO, que é o nosso caso (foto grande -> 20x20).
 * ============================================================ */
static void redimensionar_area(const unsigned char *src, int sw,
                               int x0, int y0, int rw, int rh,
                               unsigned char *dst, int dw, int dh)
{
    for (int ty = 0; ty < dh; ty++) {
        /* Faixa vertical [fy0, fy1) da fonte coberta pela linha ty */
        double fy0 = y0 + (double)rh * ty       / dh;
        double fy1 = y0 + (double)rh * (ty + 1) / dh;

        for (int tx = 0; tx < dw; tx++) {
            double fx0 = x0 + (double)rw * tx       / dw;
            double fx1 = x0 + (double)rw * (tx + 1) / dw;

            double soma = 0.0, area = 0.0;

            for (int sy = (int)fy0; sy < fy1; sy++) {
                /* Peso vertical: fração do pixel sy dentro de [fy0,fy1) */
                double py0 = (sy   > fy0) ? sy   : fy0;
                double py1 = (sy+1 < fy1) ? sy+1 : fy1;
                double wy  = py1 - py0;
                if (wy <= 0.0) continue;

                for (int sx = (int)fx0; sx < fx1; sx++) {
                    double px0 = (sx   > fx0) ? sx   : fx0;
                    double px1 = (sx+1 < fx1) ? sx+1 : fx1;
                    double wx  = px1 - px0;
                    if (wx <= 0.0) continue;

                    soma += (double)src[sy * sw + sx] * wx * wy;
                    area += wx * wy;
                }
            }

            int v = (area > 0.0) ? (int)(soma / area + 0.5) : 0;
            if (v < 0)   v = 0;
            if (v > 255) v = 255;
            dst[ty * dw + tx] = (unsigned char)v;
        }
    }
}

/* ============================================================
 * Conversão de uma imagem decodificada (escala de cinza,
 * dimensões arbitrárias) para a moldura 28x28 do MNIST.
 * ============================================================ */
static void converter_para_mnist(unsigned char *px, int w, int h,
                                 uint8_t out[IMG_SIZE])
{
    /* --- 2. Detecção de fundo claro: média dos pixels da borda.
     * O MNIST é traço CLARO em fundo ESCURO; uma foto/scan típico
     * é tinta escura em papel claro e precisa ser invertida.   */
    long soma_borda = 0;
    int  n_borda    = 0;

    for (int x = 0; x < w; x++) {
        soma_borda += px[x] + px[(h - 1) * w + x];
        n_borda += 2;
    }
    for (int y = 1; y < h - 1; y++) {
        soma_borda += px[y * w] + px[y * w + (w - 1)];
        n_borda += 2;
    }

    int fundo_claro = (n_borda > 0) && (soma_borda / n_borda > 127);
    if (fundo_claro) {
        printf("[elm_app] Fundo claro detectado — invertendo "
               "(MNIST = traço claro em fundo escuro).\n");
        for (int i = 0; i < w * h; i++)
            px[i] = (unsigned char)(255 - px[i]);
    }

    /* --- 3. Bounding box do dígito (pixels acima do limiar) --- */
    int bx0 = w, by0 = h, bx1 = -1, by1 = -1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (px[y * w + x] > LIMIAR_TINTA) {
                if (x < bx0) bx0 = x;
                if (x > bx1) bx1 = x;
                if (y < by0) by0 = y;
                if (y > by1) by1 = y;
            }

    memset(out, 0, IMG_SIZE);

    if (bx1 < 0) {
        /* Imagem vazia (tudo abaixo do limiar): devolve tudo preto */
        fprintf(stderr,
                "[elm_app] Aviso: nenhum traço encontrado na imagem.\n");
        return;
    }

    int rw = bx1 - bx0 + 1;
    int rh = by1 - by0 + 1;

    /* --- 4. Escala a bounding box para caber em 20x20,
     *        preservando a proporção --- */
    int novo_w, novo_h;
    if (rw >= rh) {
        novo_w = DIGITO_LADO;
        novo_h = (rh * DIGITO_LADO + rw / 2) / rw;
        if (novo_h < 1) novo_h = 1;
    } else {
        novo_h = DIGITO_LADO;
        novo_w = (rw * DIGITO_LADO + rh / 2) / rh;
        if (novo_w < 1) novo_w = 1;
    }

    unsigned char miniatura[DIGITO_LADO * DIGITO_LADO];
    redimensionar_area(px, w, bx0, by0, rw, rh,
                       miniatura, novo_w, novo_h);

    /* --- 5. Centraliza a miniatura na moldura 28x28 --- */
    int off_x = (IMG_W - novo_w) / 2;
    int off_y = (IMG_H - novo_h) / 2;

    for (int y = 0; y < novo_h; y++)
        for (int x = 0; x < novo_w; x++)
            out[(off_y + y) * IMG_W + (off_x + x)] =
                miniatura[y * novo_w + x];
}

/* ============================================================
 * API pública
 * ============================================================ */
int carregar_imagem_qualquer(const char *caminho,
                             uint8_t out[IMG_SIZE],
                             int *foi_convertida)
{
    if (foi_convertida) *foi_convertida = 0;

    /* .bin: caminho direto, sem conversão */
    if (tem_extensao(caminho, ".bin"))
        return carregar_bin(caminho, out);

    /* --- 1. Decodifica forçando 1 canal (escala de cinza) --- */
    int w = 0, h = 0;
    unsigned char *px = stbi_load(caminho, &w, &h, NULL, 1);
    if (!px) {
        fprintf(stderr, "[elm_app] Erro decodificando '%s': %s\n",
                caminho, stbi_failure_reason());
        return -1;
    }

    printf("[elm_app] Imagem '%s' decodificada: %dx%d pixels.\n",
           caminho, w, h);

    converter_para_mnist(px, w, h, out);
    stbi_image_free(px);

    if (foi_convertida) *foi_convertida = 1;
    return 0;
}

int salvar_bin(const char *caminho, const uint8_t img[IMG_SIZE])
{
    FILE *f = fopen(caminho, "wb");
    if (!f) {
        fprintf(stderr, "[elm_app] Não foi possível criar '%s': %s\n",
                caminho, strerror(errno));
        return -1;
    }

    size_t escritos = fwrite(img, 1, IMG_SIZE, f);
    fclose(f);

    if (escritos != IMG_SIZE) {
        fprintf(stderr, "[elm_app] Escrita incompleta em '%s'.\n", caminho);
        return -1;
    }
    return 0;
}
