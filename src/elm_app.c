/* ============================================================
 * elm_app.c  –  Aplicação unificada do co-processador ELM
 *
 * Menu interativo com três modos de operação:
 *   1. Inferência a partir de imagem em arquivo (.png/.jpg/
 *      .bmp com conversão embutida, ou .bin raw 784B)
 *   2. Inferência a partir de imagem desenhada na tela (VGA+mouse)
 *   3. Validação / benchmark sobre um dataset (subpastas 0..9)
 *
 * Os pesos (W_in, bias, beta) são carregados UMA vez na
 * inicialização, valendo para todos os modos.
 *
 * Compilação:
 *   make            (gera elm_app e golden_test)
 *
 * Uso:
 *   sudo ./elm_app <W_in.mif> <b.mif> <beta.mif> [/dev/input/eventX]
 *
 *   O dispositivo de mouse é opcional na linha de comando;
 *   se omitido, será perguntado ao entrar no modo desenho.
 *
 * A conversão de imagens é feita internamente (stb_image.h,
 * domínio público) — não há dependência de ImageMagick.
 * ============================================================ */

#define _BSD_SOURCE
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>     /* strcasecmp */
#include <stdint.h>
#include <math.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <linux/input.h>

#include "elm.h"
#include "elm_comum.h"
#include "conversor_img.h"

/* ============================================================
 * SEÇÃO 0 — Utilitários de entrada do menu
 * ============================================================ */

/* Lê uma linha do stdin, removendo o '\n'. Retorna 0/-1. */
static int ler_linha(const char *prompt, char *buf, size_t tam)
{
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(buf, (int)tam, stdin)) return -1;
    buf[strcspn(buf, "\r\n")] = '\0';
    return 0;
}

/* ============================================================
 * SEÇÃO 1 — VGA + mouse (modo desenho)
 *
 * Código herdado do mouse_paint.c, adaptado para:
 *   - inicializar o mapeamento VGA uma única vez (lazy)
 *   - retornar ao menu quando o usuário aperta ENTER no
 *     terminal (select() sobre mouse_fd e stdin)
 * ============================================================ */

#define HW_REGS_BASE (0xff200000)
#define HW_REGS_SPAN (0x00200000)
#define HW_REGS_MASK (HW_REGS_SPAN - 1)

#define VGA_SIGNALS_OFFSET 0x10
#define VGA_DATA_OFFSET    0x20

#define CANVAS_W 320
#define CANVAS_H 240
#define BRUSH_RADIUS 4

/* Caixa de desenho 224x224 = 28 blocos de 8x8 */
#define BOX_MIN_X 48
#define BOX_MAX_X 271   /* 48 + 224 - 1 */
#define BOX_MIN_Y 8
#define BOX_MAX_Y 231   /* 8 + 224 - 1  */
#define BLOCK_SIZE 8

static volatile uint32_t *vga_signals = NULL;
static volatile uint32_t *vga_data    = NULL;
static void *vga_virtual_base = MAP_FAILED;
static int   vga_mem_fd = -1;

/* Espelho do desenho no HPS (0 = fundo, 1 = traço branco) */
static uint8_t hps_canvas[CANVAS_W][CANVAS_H];

static const uint32_t color_white = (7 << 6) | (7 << 3) | 7;
static const uint32_t color_red   = (7 << 6) | (0 << 3) | 0;
static const uint32_t color_blue  = (0 << 6) | (0 << 3) | 7;

/* Mapeia os registradores do VGA (apenas na 1ª vez) */
static int vga_init(void)
{
    if (vga_signals && vga_data) return 0;   /* já mapeado */

    vga_mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (vga_mem_fd == -1) {
        fprintf(stderr, "[elm_app] Erro abrindo /dev/mem: %s\n",
                strerror(errno));
        return -1;
    }

    vga_virtual_base = mmap(NULL, HW_REGS_SPAN, PROT_READ | PROT_WRITE,
                            MAP_SHARED, vga_mem_fd, HW_REGS_BASE);
    if (vga_virtual_base == MAP_FAILED) {
        fprintf(stderr, "[elm_app] mmap do VGA falhou: %s\n",
                strerror(errno));
        close(vga_mem_fd);
        vga_mem_fd = -1;
        return -1;
    }

    vga_signals = (volatile uint32_t *)((char *)vga_virtual_base +
                  ((HW_REGS_BASE + VGA_SIGNALS_OFFSET) & HW_REGS_MASK));
    vga_data    = (volatile uint32_t *)((char *)vga_virtual_base +
                  ((HW_REGS_BASE + VGA_DATA_OFFSET) & HW_REGS_MASK));
    return 0;
}

static void vga_close(void)
{
    if (vga_virtual_base != MAP_FAILED) {
        munmap(vga_virtual_base, HW_REGS_SPAN);
        vga_virtual_base = MAP_FAILED;
    }
    if (vga_mem_fd != -1) {
        close(vga_mem_fd);
        vga_mem_fd = -1;
    }
    vga_signals = NULL;
    vga_data    = NULL;
}

/* Escreve um pixel cru no VGA */
static inline void vga_escrever(int x, int y, uint32_t cor)
{
    uint32_t data_out = (x & 0x1FF) | ((y & 0xFF) << 9) |
                        ((cor & 0x1FF) << 17);
    *vga_data    = data_out;
    *vga_signals = 1;
    *vga_signals = 0;
}

/* Pinta um pixel real: memória do HPS + VGA */
static void draw_pixel(int x, int y, uint32_t cor, uint8_t logic_val)
{
    if (x < 0 || x >= CANVAS_W || y < 0 || y >= CANVAS_H) return;
    hps_canvas[x][y] = logic_val;
    vga_escrever(x, y, cor);
}

static int is_border(int x, int y)
{
    if ((x == BOX_MIN_X || x == BOX_MAX_X) &&
        (y >= BOX_MIN_Y && y <= BOX_MAX_Y)) return 1;
    if ((y == BOX_MIN_Y || y == BOX_MAX_Y) &&
        (x >= BOX_MIN_X && x <= BOX_MAX_X)) return 1;
    return 0;
}

/* Restaura um pixel do VGA a partir do espelho do HPS
 * (usado para apagar o cursor sem destruir o desenho) */
static void restore_pixel(int x, int y)
{
    if (x < 0 || x >= CANVAS_W || y < 0 || y >= CANVAS_H) return;

    uint32_t cor = 0;                          /* fundo preto    */
    if (hps_canvas[x][y])        cor = color_white; /* traço     */
    else if (is_border(x, y))    cor = color_blue;  /* moldura   */

    vga_escrever(x, y, cor);
}

static void clear_screen(void)
{
    for (int y = 0; y < CANVAS_H; y++)
        for (int x = 0; x < CANVAS_W; x++) {
            hps_canvas[x][y] = 0;
            restore_pixel(x, y);
        }
}

/* Desenha (erase=0) ou apaga (erase=1) a mira 5x5 do cursor */
static void render_cursor(int cx, int cy, int erase)
{
    for (int i = -2; i <= 2; i++) {
        if (erase) {
            restore_pixel(cx + i, cy);
            restore_pixel(cx, cy + i);
        } else {
            int px, py;
            px = cx + i; py = cy;
            if (px >= 0 && px < CANVAS_W && py >= 0 && py < CANVAS_H)
                vga_escrever(px, py, color_red);
            px = cx; py = cy + i;
            if (px >= 0 && px < CANVAS_W && py >= 0 && py < CANVAS_H)
                vga_escrever(px, py, color_red);
        }
    }
}

static void draw_brush(int cx, int cy)
{
    for (int y = cy - BRUSH_RADIUS; y <= cy + BRUSH_RADIUS; y++)
        for (int x = cx - BRUSH_RADIUS; x <= cx + BRUSH_RADIUS; x++)
            draw_pixel(x, y, color_white, 1);
}

/* Bresenham com pincel */
static void draw_line(int x0, int y0, int x1, int y1)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    while (1) {
        draw_brush(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* Downsampling da caixa 224x224 para 28x28 (blocos 8x8).
 * Um bloco vira 255 se tiver >= 3 pixels brancos. */
static void construir_matriz_mnist(uint8_t img[IMG_SIZE])
{
    for (int r = 0; r < IMG_H; r++) {
        for (int c = 0; c < IMG_W; c++) {
            int brancos = 0;
            for (int by = 0; by < BLOCK_SIZE; by++)
                for (int bx = 0; bx < BLOCK_SIZE; bx++) {
                    int cx = BOX_MIN_X + c * BLOCK_SIZE + bx;
                    int cy = BOX_MIN_Y + r * BLOCK_SIZE + by;
                    if (hps_canvas[cx][cy] > 0) brancos++;
                }
            /* 255 corresponde à escala usada no treino do ELM */
            img[r * IMG_W + c] = (brancos >= 3) ? 255 : 0;
        }
    }
}

/* Classifica o desenho atual e imprime o resultado */
static void classificar_desenho(void)
{
    uint8_t img[IMG_SIZE];
    construir_matriz_mnist(img);
    imprimir_preview_ascii(img);

    printf("[elm_app] Disparando inferência...\n");

    int digito = -1;
    long long lat = 0;
    int r = inferir_digito(img, &digito, &lat);

    if (r == 1) {
        printf("\n==================================\n");
        printf(">>> DÍGITO CLASSIFICADO: %d <<<\n", digito);
        printf("    (latência: %lld us)\n", lat);
        printf("==================================\n\n");
    } else if (r == -1) {
        printf("[elm_app] ERRO: hardware reportou falha na inferência.\n");
    } else if (r == -2) {
        printf("[elm_app] ERRO: driver ELM não inicializado.\n");
    } else {
        printf("[elm_app] TIMEOUT: hardware não subiu a flag DONE "
               "(STATUS=0x%08X).\n", elm_ler_status_raw());
    }
}

/* ------------------------------------------------------------
 * MODO 2 — Inferência a partir de desenho na tela
 * ------------------------------------------------------------ */
static void modo_desenho(const char *mouse_dev_padrao)
{
    char dev[256];

    if (mouse_dev_padrao && mouse_dev_padrao[0]) {
        strncpy(dev, mouse_dev_padrao, sizeof(dev) - 1);
        dev[sizeof(dev) - 1] = '\0';
    } else {
        if (ler_linha("Dispositivo do mouse (ex.: /dev/input/event0): ",
                      dev, sizeof(dev)) != 0 || dev[0] == '\0') {
            printf("[elm_app] Dispositivo inválido.\n");
            return;
        }
    }

    if (vga_init() != 0) return;

    int mouse_fd = open(dev, O_RDONLY);
    if (mouse_fd == -1) {
        fprintf(stderr, "[elm_app] Erro abrindo '%s': %s\n",
                dev, strerror(errno));
        return;
    }

    printf("\n--- MODO DESENHO ---\n");
    printf("  Botão esquerdo : desenhar\n");
    printf("  Botão direito  : limpar a tela\n");
    printf("  Botão do meio  : classificar o dígito\n");
    printf("  ENTER (aqui)   : voltar ao menu\n\n");

    clear_screen();

    int cursor_x = CANVAS_W / 2;
    int cursor_y = CANVAS_H / 2;
    int last_x = cursor_x, last_y = cursor_y;
    int is_drawing = 0;

    render_cursor(cursor_x, cursor_y, 0);

    struct input_event ev;
    int sair = 0;

    while (!sair) {
        /* Espera evento do mouse OU tecla no terminal */
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(mouse_fd, &fds);
        FD_SET(STDIN_FILENO, &fds);
        int maxfd = (mouse_fd > STDIN_FILENO) ? mouse_fd : STDIN_FILENO;

        if (select(maxfd + 1, &fds, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) continue;
            perror("[elm_app] select");
            break;
        }

        /* ENTER no terminal -> volta ao menu */
        if (FD_ISSET(STDIN_FILENO, &fds)) {
            char descarte[64];
            if (fgets(descarte, sizeof(descarte), stdin) == NULL) { /* EOF */ }
            sair = 1;
            continue;
        }

        if (!FD_ISSET(mouse_fd, &fds)) continue;
        if (read(mouse_fd, &ev, sizeof(ev)) <= 0) continue;

        /* --- Cliques --- */
        if (ev.type == EV_KEY) {
            render_cursor(cursor_x, cursor_y, 1);   /* apaga cursor */

            if (ev.code == BTN_LEFT) {
                is_drawing = ev.value;
                if (is_drawing) {
                    last_x = cursor_x;
                    last_y = cursor_y;
                    draw_brush(cursor_x, cursor_y);
                }
            }
            else if (ev.code == BTN_RIGHT && ev.value == 1) {
                printf("[elm_app] Limpando a tela...\n");
                clear_screen();
            }
            else if (ev.code == BTN_MIDDLE && ev.value == 1) {
                classificar_desenho();
            }

            render_cursor(cursor_x, cursor_y, 0);   /* redesenha   */
        }

        /* --- Movimento --- */
        if (ev.type == EV_REL) {
            render_cursor(cursor_x, cursor_y, 1);

            if (ev.code == REL_X) cursor_x += ev.value;
            if (ev.code == REL_Y) cursor_y += ev.value;

            if (cursor_x < 0)          cursor_x = 0;
            if (cursor_x >= CANVAS_W)  cursor_x = CANVAS_W - 1;
            if (cursor_y < 0)          cursor_y = 0;
            if (cursor_y >= CANVAS_H)  cursor_y = CANVAS_H - 1;

            if (is_drawing) {
                draw_line(last_x, last_y, cursor_x, cursor_y);
                last_x = cursor_x;
                last_y = cursor_y;
            }

            render_cursor(cursor_x, cursor_y, 0);
        }
    }

    /* Limpa o cursor da tela antes de sair do modo */
    render_cursor(cursor_x, cursor_y, 1);
    close(mouse_fd);
    printf("[elm_app] Saindo do modo desenho.\n");
}

/* ============================================================
 * SEÇÃO 2 — MODO 1: Inferência a partir de arquivo de imagem
 *
 * Aceita .png, .jpg, .jpeg, .bmp (convertidos internamente
 * via stb_image, sem dependências externas) ou .bin raw
 * (784 bytes, usado diretamente). Quando há conversão, o
 * resultado também é salvo como .bin ao lado do original —
 * útil para montar o dataset do benchmark sem ImageMagick.
 * ============================================================ */
static void modo_arquivo(void)
{
    char caminho[512];

    printf("\n--- MODO ARQUIVO ---\n");
    printf("Formatos aceitos: .png, .jpg, .bmp (conversão automática)\n");
    printf("                  .bin raw 784 bytes (uso direto)\n\n");

    if (ler_linha("Caminho da imagem: ", caminho, sizeof(caminho)) != 0 ||
        caminho[0] == '\0') {
        printf("[elm_app] Caminho inválido.\n");
        return;
    }

    uint8_t img[IMG_SIZE];
    int foi_convertida = 0;
    if (carregar_imagem_qualquer(caminho, img, &foi_convertida) != 0)
        return;

    imprimir_preview_ascii(img);

    /* Salva o .bin convertido ao lado do arquivo original */
    if (foi_convertida) {
        char caminho_bin[520];
        caminho_com_extensao_bin(caminho, caminho_bin,
                                 sizeof(caminho_bin));
        if (salvar_bin(caminho_bin, img) == 0)
            printf("[elm_app] Convertida salva em '%s' "
                   "(reutilizável no benchmark).\n", caminho_bin);
    }

    printf("[elm_app] Disparando inferência...\n");

    int digito = -1;
    long long lat = 0;
    int r = inferir_digito(img, &digito, &lat);

    if (r == 1) {
        printf("\n==================================\n");
        printf(">>> DÍGITO CLASSIFICADO: %d <<<\n", digito);
        printf("    (latência: %lld us)\n", lat);
        printf("==================================\n\n");
    } else if (r == -1) {
        printf("[elm_app] ERRO: hardware reportou falha na inferência.\n");
    } else if (r == -2) {
        printf("[elm_app] ERRO: driver ELM não inicializado.\n");
    } else {
        printf("[elm_app] TIMEOUT: hardware não subiu a flag DONE "
               "(STATUS=0x%08X).\n", elm_ler_status_raw());
    }
}

/* ============================================================
 * SEÇÃO 3 — MODO 3: Benchmark sobre dataset
 *
 * Estrutura esperada:
 *   <pasta_raiz>/0/img.bin ... <pasta_raiz>/9/img.bin
 * ============================================================ */

#define MAX_IMAGENS 10000

typedef struct {
    char caminho[512];
    int  label;
} entrada_t;

typedef struct {
    char arquivo[512];
    int  label_esperado;
    int  digito_predito;
    int  acerto;
    long long latencia_us;
} resultado_t;

static int varrer_subpasta(const char *pasta, int label,
                           entrada_t *lista, int inicio, int max_total)
{
    DIR *dir = opendir(pasta);
    if (!dir) return -1;

    struct dirent *ent;
    int n = 0;

    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        size_t len = strlen(ent->d_name);
        if (len < 5) continue;
        if (strcasecmp(ent->d_name + len - 4, ".bin") != 0) continue;

        int idx = inicio + n;
        if (idx >= max_total) {
            fprintf(stderr,
                    "[elm_app] Limite de %d imagens atingido — "
                    "restantes ignoradas.\n", max_total);
            break;
        }

        snprintf(lista[idx].caminho, sizeof(lista[idx].caminho),
                 "%s/%s", pasta, ent->d_name);
        lista[idx].label = label;
        n++;
    }

    closedir(dir);
    return n;
}

static void imprimir_estatisticas(resultado_t *res, int total)
{
    if (total == 0) {
        printf("[elm_app] Nenhuma imagem processada.\n");
        return;
    }

    int acertos = 0;
    for (int i = 0; i < total; i++) acertos += res[i].acerto;
    double acuracia = 100.0 * acertos / total;

    double soma = 0.0;
    long long lat_min = -1, lat_max = -1;
    int n_lat = 0;

    for (int i = 0; i < total; i++) {
        if (res[i].latencia_us > 0) {
            soma += (double)res[i].latencia_us;
            if (lat_min < 0 || res[i].latencia_us < lat_min)
                lat_min = res[i].latencia_us;
            if (res[i].latencia_us > lat_max)
                lat_max = res[i].latencia_us;
            n_lat++;
        }
    }

    double media  = (n_lat > 0) ? soma / n_lat : 0.0;
    double desvio = 0.0;
    if (n_lat > 1) {
        double var = 0.0;
        for (int i = 0; i < total; i++) {
            if (res[i].latencia_us > 0) {
                double d = (double)res[i].latencia_us - media;
                var += d * d;
            }
        }
        desvio = sqrt(var / n_lat);
    }
    double throughput = (media > 0.0) ? 1e6 / media : 0.0;

    int acertos_classe[N_DIGITOS] = {0};
    int total_classe[N_DIGITOS]   = {0};
    for (int i = 0; i < total; i++) {
        int l = res[i].label_esperado;
        if (l >= 0 && l < N_DIGITOS) {
            total_classe[l]++;
            if (res[i].acerto) acertos_classe[l]++;
        }
    }

    printf("\n");
    printf("╔══════════════════════════════════════════════╗\n");
    printf("║          RESULTADOS DO BENCHMARK             ║\n");
    printf("╠══════════════════════════════════════════════╣\n");
    printf("║  Imagens processadas : %6d               ║\n", total);
    printf("║  Acertos             : %6d               ║\n", acertos);
    printf("║  Acurácia            : %6.2f %%             ║\n", acuracia);
    printf("╠══════════════════════════════════════════════╣\n");
    printf("║  Latência média      : %8.1f us          ║\n", media);
    printf("║  Desvio padrão       : %8.1f us          ║\n", desvio);
    printf("║  Latência mínima     : %8lld us          ║\n", lat_min);
    printf("║  Latência máxima     : %8lld us          ║\n", lat_max);
    printf("║  Throughput          : %8.2f img/s        ║\n", throughput);
    printf("╠══════════════════════════════════════════════╣\n");
    printf("║  Acurácia por classe:                        ║\n");
    for (int d = 0; d < N_DIGITOS; d++) {
        double acc = (total_classe[d] > 0)
                   ? 100.0 * acertos_classe[d] / total_classe[d] : 0.0;
        printf("║    Dígito %d : %4d/%4d  (%6.2f %%)         ║\n",
               d, acertos_classe[d], total_classe[d], acc);
    }
    printf("╚══════════════════════════════════════════════╝\n");
}

static void salvar_csv(resultado_t *res, int total, const char *caminho_csv)
{
    FILE *f = fopen(caminho_csv, "w");
    if (!f) {
        fprintf(stderr, "[elm_app] Não foi possível criar '%s': %s\n",
                caminho_csv, strerror(errno));
        return;
    }

    fprintf(f, "arquivo,label_esperado,digito_predito,acerto,latencia_us\n");
    for (int i = 0; i < total; i++)
        fprintf(f, "%s,%d,%d,%d,%lld\n",
                res[i].arquivo, res[i].label_esperado,
                res[i].digito_predito, res[i].acerto,
                res[i].latencia_us);

    fclose(f);
    printf("[elm_app] Resultados individuais salvos em '%s'.\n", caminho_csv);
}

static void modo_benchmark(void)
{
    char pasta_raiz[512];

    printf("\n--- MODO BENCHMARK ---\n");
    printf("Estrutura esperada: <pasta>/0/*.bin ... <pasta>/9/*.bin\n\n");

    if (ler_linha("Pasta raiz do dataset: ", pasta_raiz,
                  sizeof(pasta_raiz)) != 0 || pasta_raiz[0] == '\0') {
        printf("[elm_app] Pasta inválida.\n");
        return;
    }

    entrada_t   *lista      = calloc(MAX_IMAGENS, sizeof(entrada_t));
    resultado_t *resultados = calloc(MAX_IMAGENS, sizeof(resultado_t));
    uint8_t     *img        = calloc(IMG_SIZE, sizeof(uint8_t));

    if (!lista || !resultados || !img) {
        fprintf(stderr, "[elm_app] Falha de alocação de memória.\n");
        goto fim;
    }

    /* --- Varredura das subpastas 0..9 --- */
    printf("[elm_app] Varrendo dataset em '%s'...\n", pasta_raiz);

    int total_imagens = 0;
    for (int d = 0; d < N_DIGITOS; d++) {
        char subpasta[600];
        snprintf(subpasta, sizeof(subpasta), "%s/%d", pasta_raiz, d);

        int n = varrer_subpasta(subpasta, d, lista,
                                total_imagens, MAX_IMAGENS);
        if (n < 0) {
            fprintf(stderr,
                    "[elm_app] Aviso: subpasta '%s' não encontrada.\n",
                    subpasta);
            continue;
        }
        printf("[elm_app]   dígito %d: %d imagem(ns)\n", d, n);
        total_imagens += n;
    }

    if (total_imagens == 0) {
        fprintf(stderr,
                "[elm_app] Nenhuma imagem .bin encontrada em '%s'.\n",
                pasta_raiz);
        goto fim;
    }
    printf("[elm_app] Total: %d imagens a processar.\n\n", total_imagens);

    /* --- Loop de inferências --- */
    int processadas = 0;

    for (int i = 0; i < total_imagens; i++) {
        if (i > 0 && i % 50 == 0)
            printf("[elm_app] Progresso: %d/%d\n", i, total_imagens);

        if (carregar_bin(lista[i].caminho, img) != 0) {
            fprintf(stderr, "[elm_app] Pulando '%s'.\n", lista[i].caminho);
            continue;
        }

        int digito = -1;
        long long lat = 0;
        int r = inferir_digito(img, &digito, &lat);

        resultado_t *res = &resultados[processadas];
        strncpy(res->arquivo, lista[i].caminho, sizeof(res->arquivo) - 1);
        res->arquivo[sizeof(res->arquivo) - 1] = '\0';
        res->label_esperado = lista[i].label;
        res->latencia_us    = lat;

        if (r == 1) {
            res->digito_predito = digito;
            res->acerto = (digito == lista[i].label) ? 1 : 0;
        } else {
            if (r == -1)
                fprintf(stderr, "[elm_app] Erro de hardware em '%s'.\n",
                        lista[i].caminho);
            else
                fprintf(stderr, "[elm_app] Timeout em '%s'.\n",
                        lista[i].caminho);
            res->digito_predito = -1;
            res->acerto = 0;
        }

        processadas++;
    }

    imprimir_estatisticas(resultados, processadas);
    salvar_csv(resultados, processadas, "benchmark.csv");

fim:
    free(lista);
    free(resultados);
    free(img);
}

/* ============================================================
 * SEÇÃO 4 — Menu principal
 * ============================================================ */
static void imprimir_menu(void)
{
    printf("\n");
    printf("╔══════════════════════════════════════════════╗\n");
    printf("║        APLICAÇÃO UNIFICADA - ELM             ║\n");
    printf("╠══════════════════════════════════════════════╣\n");
    printf("║  1. Inferência a partir de arquivo de imagem ║\n");
    printf("║  2. Inferência por desenho na tela (mouse)   ║\n");
    printf("║  3. Validação / benchmark (dataset)          ║\n");
    printf("║  4. Recarregar pesos no hardware             ║\n");
    printf("║  0. Sair                                     ║\n");
    printf("╚══════════════════════════════════════════════╝\n");
}

int main(int argc, char *argv[])
{
    if (argc != 4 && argc != 5) {
        fprintf(stderr,
            "Uso: sudo %s <W_in.mif> <b.mif> <beta.mif> [/dev/input/eventX]\n\n"
            "  O dispositivo de mouse é opcional; se omitido, será\n"
            "  perguntado ao entrar no modo desenho.\n"
            "  Exemplo: sudo %s W_in_q.mif b_q.mif beta_q.mif "
            "/dev/input/event0\n",
            argv[0], argv[0]);
        return 1;
    }

    const char *path_W    = argv[1];
    const char *path_b    = argv[2];
    const char *path_beta = argv[3];
    const char *mouse_dev = (argc == 5) ? argv[4] : NULL;

    printf("╔══════════════════════════════════════════════╗\n");
    printf("║   Co-processador ELM — Aplicação Unificada   ║\n");
    printf("╚══════════════════════════════════════════════╝\n\n");

    /* --- Inicialização única do driver e dos pesos --- */
    printf("[elm_app] Inicializando driver ELM...\n");
    if (elm_init() != ELM_OK) {
        fprintf(stderr, "[elm_app] Falha fatal: elm_init() retornou erro.\n");
        return 1;
    }
    printf("[elm_app] Driver inicializado.\n\n");

    if (carregar_pesos_hardware(path_W, path_b, path_beta) != 0) {
        fprintf(stderr, "[elm_app] Falha ao carregar os pesos. Encerrando.\n");
        elm_close();
        return 1;
    }

    /* --- Loop do menu --- */
    char opcao[16];
    int rodando = 1;

    while (rodando) {
        imprimir_menu();
        if (ler_linha("Escolha uma opção: ", opcao, sizeof(opcao)) != 0)
            break;   /* EOF no stdin */

        switch (atoi(opcao)) {
        case 1:
            modo_arquivo();
            break;
        case 2:
            modo_desenho(mouse_dev);
            break;
        case 3:
            modo_benchmark();
            break;
        case 4:
            if (carregar_pesos_hardware(path_W, path_b, path_beta) != 0)
                fprintf(stderr, "[elm_app] Falha ao recarregar pesos.\n");
            break;
        case 0:
            rodando = 0;
            break;
        default:
            printf("[elm_app] Opção inválida.\n");
        }
    }

    /* --- Encerramento --- */
    vga_close();
    elm_close();
    printf("[elm_app] Driver encerrado. Até mais!\n");
    return 0;
}
