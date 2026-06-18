# Marco 3 — Co-Processador ELM no DE1-SoC

Aplicação unificada para o **co-processador ELM (Extreme Learning Machine)** implementado no FPGA da placa DE1-SoC (ARM + FPGA), com suporte a três modos de operação: inferência a partir de arquivo, desenho interativo com mouse e validação/benchmark sobre datasets.

---

## Índice

- [Levantamento de Requisitos](#levantamento-de-requisitos)
- [Especificação de Hardware](#especificação-de-hardware)
- [Softwares e Versões](#softwares-e-versões)
- [Estrutura do Projeto](#estrutura-do-projeto)
- [Instalação e Configuração do Ambiente](#instalação-e-configuração-do-ambiente)
- [Compilação](#compilação)
- [Uso](#uso)
- [Modos de Operação](#modos-de-operação)
- [Arquitetura Final](#arquitetura-final)
- [Testes de Funcionamento](#testes-de-funcionamento)
- [Resultados de Acurácia e Desempenho](#resultados-de-acurácia-e-desempenho)
- [Análise dos Resultados e Gargalos](#análise-dos-resultados-e-gargalos)
- [Troubleshooting](#troubleshooting)

---

## Levantamento de Requisitos

### Requisitos Funcionais

| ID | Requisito |
|----|-----------|
| RF01 | A aplicação deve classificar imagens de dígitos (0–9) em escala de cinza 28×28 pixels usando o co-processador ELM no FPGA |
| RF02 | Deve suportar imagens nos formatos PNG, JPG, BMP e BIN como entrada |
| RF03 | Deve oferecer modo de inferência a partir de arquivo no disco |
| RF04 | Deve oferecer modo de inferência a partir de dígito desenhado interativamente via mouse e exibido no VGA |
| RF05 | Deve oferecer modo de validação/benchmark sobre dataset estruturado em subpastas 0..9 |
| RF06 | O benchmark deve calcular acurácia (%), latência média, desvio padrão e throughput (img/s) |
| RF07 | Os resultados do benchmark devem ser salvos em arquivo CSV |
| RF08 | Os pesos da rede (W_in, b, β) devem ser carregados a partir de arquivos no formato MIF |
| RF09 | A aplicação deve exibir a imagem inferida usando o IP-Core VGA |
| RF10 | A comunicação com o co-processador deve ser feita via MMIO através do driver em Assembly |

### Requisitos Não-Funcionais

| ID | Requisito |
|----|-----------|
| RNF01 | O driver de acesso ao hardware deve ser implementado em Assembly ARM |
| RNF02 | A aplicação deve ser desenvolvida em linguagem C (C99) |
| RNF03 | Os pesos devem ser representados em ponto fixo Q4.12 (16 bits) |
| RNF04 | A imagem de entrada é de 784 bytes (28×28 pixels, 1 byte por pixel) |
| RNF05 | A aplicação deve funcionar em Linux ARM rodando no HPS da DE1-SoC |
| RNF06 | O acesso ao hardware exige privilégio root (acesso a `/dev/mem`) |

### Restrições

- Plataforma alvo: DE1-SoC (ARM Cortex-A9 + Cyclone V FPGA)
- O modelo ELM (pesos) é fornecido pronto — não há treinamento
- A conversão de imagens deve ser feita sem dependências externas (uso de `stb_image.h`)

---

## Especificação de Hardware

| Componente | Especificação |
|------------|---------------|
| Placa | Altera/Intel DE1-SoC (P0150) |
| FPGA | Cyclone V SE 5CSEMA5F31C6 |
| HPS (CPU) | ARM Cortex-A9 dual-core, 800 MHz |
| Memória RAM | 1 GB DDR3 |
| Display | `[PREENCHER: ex. monitor VGA 1024×768 60 Hz — marca/modelo]` |
| Mouse | `[PREENCHER: ex. mouse USB Logitech B100 — identificado como /dev/input/event0]` |
| Cabo VGA | `[PREENCHER: se relevante]` |

> **Nota:** preencha com os dados exatos do equipamento usado nos testes para garantir reprodutibilidade.

---

## Softwares e Versões

### No host (compilação cruzada)

| Software | Versão | Finalidade |
|----------|--------|------------|
| `arm-linux-gnueabihf-gcc` | `[PREENCHER: ex. 11.4.0]` | Compilação cruzada C |
| `arm-linux-gnueabihf-as` | `[PREENCHER: ex. 2.38]` | Montagem Assembly ARM |
| Quartus Prime | `[PREENCHER: ex. 21.1 Lite]` | Síntese do co-processador Verilog |
| ModelSim / Questa | `[PREENCHER: ex. 2021.2]` | Simulação do RTL |
| GNU Make | `[PREENCHER: ex. 4.3]` | Automação do build |
| Ubuntu / Debian host | `[PREENCHER: ex. Ubuntu 22.04 LTS]` | Sistema operacional do host |

### No target (DE1-SoC)

| Software | Versão | Finalidade |
|----------|--------|------------|
| Linux kernel | `[PREENCHER: ex. 4.14.130-ltsi]` | SO do HPS |
| `gcc` (nativo) | `[PREENCHER: se compilado nativamente]` | Compilação nativa (alternativa) |
| `libc` (glibc) | `[PREENCHER: ex. 2.27]` | Biblioteca C padrão |
| `libm` | (incluída na glibc) | Funções matemáticas (`sqrt`) |
| `librt` | (incluída na glibc) | `clock_gettime` em kernels antigos |

---

## Estrutura do Projeto

```
Marco_3-CoProcessador_ELM-DE1-SoC/
├── README.md                        # Este arquivo
├── Problema-MI-SD-2026-1(V02).pdf   # Especificação do projeto
├── imagens/                         # Imagens de teste
└── src/
    ├── Makefile                     # Script de build
    ├── elm_app.c                    # Aplicação principal (menu + 3 modos)
    ├── elm.c / elm.h                # API C do co-processador
    ├── elm_comum.c / elm_comum.h    # Funções compartilhadas (MIF, BIN, polling)
    ├── elm_driver.s                 # Driver de baixo nível em Assembly ARM
    ├── conversor_img.c / conversor_img.h  # Pipeline de conversão de imagens
    ├── stb_image.h                  # Decodificador PNG/JPG/BMP (domínio público)
    ├── W_in_q.mif                   # Pesos de entrada (784×128, Q4.12)
    ├── b_q.mif                      # Bias (128 valores, Q4.12)
    ├── beta_q.mif                   # Beta/saída (10×128, Q4.12)
    ├── benchmark.csv                # Resultados do último benchmark executado
    └── *.bin / *.png                # Imagens de teste
```

---

## Instalação e Configuração do Ambiente

### 1. Configurar a toolchain de compilação cruzada (no host)

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install gcc-arm-linux-gnueabihf binutils-arm-linux-gnueabihf

# Verifique a instalação
arm-linux-gnueabihf-gcc --version
arm-linux-gnueabihf-as --version
```

### 2. Programar o FPGA com o co-processador ELM

```bash
# No Quartus, abra o projeto e compile:
#   Processing > Start Compilation
# Em seguida, programe a DE1-SoC via JTAG:
#   Tools > Programmer > Start
# [PREENCHER: caminho do arquivo .sof gerado]
```

> O FPGA **deve estar programado antes** de iniciar a aplicação. Sem a programação correta, `elm_init()` mapeia a região de memória errada e todas as operações produzem resultados inválidos.

### 3. Transferir os arquivos para a placa

```bash
# Via SCP (substitua pelo IP real da placa)
scp -r src/ root@<IP_DA_PLACA>:/home/root/elm_app/

# Ou via cartão SD / pendrive, copiando para /home/root/elm_app/
```

### 4. Verificar o dispositivo do mouse (na DE1-SoC)

```bash
# Liste os dispositivos de entrada disponíveis
ls -la /dev/input/

# Identifique qual é o mouse (normalmente event0 ou event1)
cat /proc/bus/input/devices | grep -A5 "Mouse"
```

### 5. Verificar permissões de `/dev/mem`

```bash
# A aplicação exige root para acessar /dev/mem
# Confirme que sudo está disponível ou execute como root:
sudo -l
```

---

## Compilação

### Compilação cruzada (no host x86_64, recomendado)

```bash
cd src
make CROSS=1        # gera elm_app e golden_test para ARM
```

### Compilação nativa (diretamente na DE1-SoC)

```bash
cd src
make                # usa o gcc local do ARM Linux
```

### Alvos disponíveis

```bash
make                  # compila tudo (elm_app + golden_test)
make elm_app          # apenas a aplicação unificada
make clean            # remove binários e arquivos objeto
make CROSS=1          # força compilação cruzada
```

### Flags de compilação

| Flag | Valor | Observação |
|------|-------|------------|
| `-std` | `gnu99` | C99 com extensões GNU |
| `-O0` | (debug) | Troque por `-O2` para medir desempenho real |
| `-Wall -Wextra` | ativados | Todos os warnings habilitados |
| `-lm` | linkado | Biblioteca matemática (`sqrt`) |
| `-lrt` | linkado | Relógio de alta resolução (`clock_gettime`) |

> **Atenção:** os benchmarks deste documento foram coletados com `-O0`. Com `-O2`, o overhead do HPS diminui, mas o gargalo principal (tempo de processamento no FPGA) permanece igual.

---

## Uso

```bash
cd src
sudo ./elm_app <W_in.mif> <b.mif> <beta.mif> [/dev/input/eventX]
```

**Argumentos:**

| Argumento | Obrigatório | Descrição |
|-----------|-------------|-----------|
| `W_in.mif` | Sim | Pesos da camada de entrada (784×128) |
| `b.mif` | Sim | Bias da camada oculta (128 valores) |
| `beta.mif` | Sim | Pesos da camada de saída (10×128) |
| `/dev/input/eventX` | Não | Dispositivo do mouse (solicitado no Modo 2 se omitido) |

**Exemplo:**

```bash
sudo ./elm_app W_in_q.mif b_q.mif beta_q.mif /dev/input/event0
```

---

## Modos de Operação

Ao iniciar, a aplicação carrega os pesos no hardware e exibe um menu com 4 opções:

```
╔══════════════════════════════════════════════╗
║        APLICAÇÃO UNIFICADA - ELM             ║
╠══════════════════════════════════════════════╣
║  1. Inferência a partir de arquivo de imagem ║
║  2. Inferência por desenho na tela (mouse)   ║
║  3. Validação / benchmark (dataset)          ║
║  4. Recarregar pesos no hardware             ║
║  0. Sair                                     ║
╚══════════════════════════════════════════════╝
```

---

### Modo 1 — Inferência a partir de arquivo

Carrega uma imagem do disco, converte para o formato MNIST (28×28, escala de cinza) e classifica.

**Formatos aceitos:**

| Formato | Processamento |
|---------|---------------|
| `.bin` | Lido diretamente (784 bytes raw, sem conversão) |
| `.png`, `.jpg`, `.bmp` | Decodificado, convertido para cinza, redimensionado para 28×28, centralizado |

**Pipeline de conversão (para PNG/JPG/BMP):**
1. Decodificação via `stb_image.h` (forçado para 1 canal — escala de cinza)
2. Detecção de fundo claro (média dos pixels da borda) — inverte se necessário para o padrão MNIST (traço claro, fundo escuro)
3. Localização da bounding box do dígito (pixels acima do limiar de 50)
4. Redimensionamento da bounding box para 20×20 (filtro de média de área, preserva proporção)
5. Centralização em moldura 28×28

**Saída:**
```
>>> DÍGITO CLASSIFICADO: 5 <<<
    (latência: 16800 us)
```

A imagem convertida é salva automaticamente como `.bin` ao lado do arquivo original, para reutilização no benchmark.

---

### Modo 2 — Inferência por desenho (VGA + mouse)

Exibe um canvas 224×224 no display VGA e permite desenhar um dígito com o mouse para classificação.

**Controles:**

| Ação | Efeito |
|------|--------|
| Botão esquerdo + movimento | Desenhar traço (pincel 4px de raio) |
| Botão direito | Limpar a tela |
| Botão do meio | Classificar o dígito atual |
| ENTER (no terminal) | Voltar ao menu |

**Mapeamento visual → MNIST:** o canvas 224×224 é dividido em blocos 8×8. Cada bloco vira um pixel 28×28: `255` se tiver ≥ 3 pixels brancos, `0` caso contrário.

---

### Modo 3 — Validação / Benchmark

Processa um dataset estruturado em subpastas e calcula métricas de acurácia e desempenho.

**Estrutura esperada:**
```
dataset/
├── 0/  *.bin
├── 1/  *.bin
...
└── 9/  *.bin
```

**Métricas calculadas:**
- Acurácia geral (%) e por dígito
- Latência média, desvio padrão, mínima e máxima (µs)
- Throughput (imagens/segundo)

**Saída:** relatório no terminal + arquivo `benchmark.csv` com resultado por imagem.

---

### Modo 4 — Recarregar pesos

Reenvia W_in, b e β ao co-processador sem reiniciar a aplicação. Útil após reset inesperado do FPGA.

---

## Arquitetura Final

### Visão geral do sistema

```
┌─────────────────────────────────────────────────────────┐
│                        DE1-SoC                          │
│                                                         │
│  ┌──────────────── HPS (ARM Cortex-A9) ───────────────┐ │
│  │                                                     │ │
│  │  elm_app.c (aplicação C)                           │ │
│  │    ├── Modo 1: arquivo → inferência                │ │
│  │    ├── Modo 2: VGA + mouse → inferência            │ │
│  │    └── Modo 3: dataset → benchmark                 │ │
│  │         │                                          │ │
│  │  elm.h / elm.c (API C)                             │ │
│  │    ├── elm_init()    ──► open(/dev/mem) + mmap()   │ │
│  │    ├── elm_carregar_imagem()                       │ │
│  │    ├── elm_disparar_inferencia()                   │ │
│  │    └── elm_obter_resultado() ◄── polling STATUS    │ │
│  │         │                                          │ │
│  │  elm_driver.s (Assembly ARM)                       │ │
│  │    ├── elm_init_asm()   → SYS_OPEN + SYS_MMAP2    │ │
│  │    ├── elm_close_asm()  → SYS_MUNMAP + SYS_CLOSE  │ │
│  │    └── processar_hardware_asm() → R/W nos PIOs     │ │
│  │         │                                          │ │
│  └─────────┼────────────────────────────────────────── │
│            │  Lightweight HPS-to-FPGA Bridge           │
│            │  Base: 0xFF200000  Span: 0x200000         │
│            │                                           │
│  ┌─────────▼──────────── FPGA ────────────────────── ┐ │
│  │                                                    │ │
│  │  PIOs de interface (offsets do base):              │ │
│  │    PIO_DATA_IN   @ +0x30  ← instrução + dados     │ │
│  │    PIO_DATA_OUT  @ +0x40  → status + resultado     │ │
│  │    PIO_SIGNALS   @ +0x50  ← pulsos de controle    │ │
│  │                                                    │ │
│  │  Co-processador ELM                                │ │
│  │    ├── FSM de controle                             │ │
│  │    ├── Datapath MAC (Q4.12)                        │ │
│  │    ├── Memórias: img (784B), W_in, b, β            │ │
│  │    ├── Ativação aproximada (tanh)                  │ │
│  │    └── Argmax → predição [0..9]                    │ │
│  │                                                    │ │
│  │  IP-Core VGA                                       │ │
│  │    Base: 0xFF200000 + 0x10 (signals)               │ │
│  │           0xFF200000 + 0x20 (data)                 │ │
│  └────────────────────────────────────────────────── ┘ │
└─────────────────────────────────────────────────────────┘
```

### Protocolo de comunicação HPS → FPGA

Cada operação segue um handshake de três fases via `processar_hardware_asm()`:

1. **Escreve** os dados + opcode em `PIO_DATA_IN`
2. **Pulsa** `PIO_SIGNALS` bit 0 (`pulse_hw`) — sinaliza dado válido ao FPGA
3. **Pulsa** `PIO_SIGNALS` bit 1 (`clear_hw`) — limpa o sinal após confirmação

Para leitura de status, basta ler `PIO_DATA_OUT` sem pulso.

### Mapa de registradores (PIO_SIGNALS)

| Bit | Nome | Ação |
|-----|------|------|
| 0 | `write_en` | Pulso: indica dado válido em DATA_IN |
| 1 | `clr_operation` | Pulso: limpa flags DONE/ERROR |
| 2 | `reset` | Pulso: reinicia a FSM do co-processador |

### Sequência de inferência (workaround do bug de 1ª execução)

O hardware apresenta comportamento conhecido: a primeira inferência após reset retorna DONE instantaneamente com resultado inválido. A solução adotada é:

```
elm_limpar_hardware()         ← limpa flags DONE/ERROR pendentes
elm_resetar_hardware()        ← reinicia a FSM
elm_carregar_imagem(img)      ← inferência "fantasma" (resultado descartado)
elm_disparar_inferencia()
usleep(20000)                 ← aguarda 20 ms o hardware "engasgar"
elm_carregar_imagem(img)      ← inferência real
elm_disparar_inferencia()
aguardar_resultado()          ← polling até DONE/ERROR/timeout (500 ms)
```

Este `usleep(20000)` é o **principal determinante da latência** observada (~16,8 ms por chamada).

### Formato de ponto fixo Q4.12

Todos os pesos são armazenados em Q4.12: inteiro com sinal de 16 bits onde os 4 bits mais significativos representam a parte inteira e os 12 bits menos significativos a parte fracionária. Fator de escala: `valor_real = valor_int16 / 4096`.

---

## Testes de Funcionamento

### Teste de estabilidade (requisito do Marco 2, mantido no Marco 3)

Verifica que a mesma imagem produz o mesmo resultado em execuções consecutivas:

```bash
# Classifica a mesma imagem 10 vezes e verifica consistência
for i in $(seq 1 10); do
    sudo ./elm_app W_in_q.mif b_q.mif beta_q.mif <<EOF
1
quatro.bin

0
EOF
done
```

> Resultado esperado: o mesmo dígito em todas as 10 execuções. Qualquer variação indica instabilidade na FSM ou no protocolo de handshake.

### Teste de sanidade do Modo 1

```bash
# Classifica uma imagem conhecida e verifica o resultado
sudo ./elm_app W_in_q.mif b_q.mif beta_q.mif <<EOF
1
quatro.bin

0
EOF
# Resultado esperado: >>> DÍGITO CLASSIFICADO: 4 <<<
```

### Teste do Modo 3 — Benchmark completo

```bash
# Estrutura mínima para teste:
mkdir -p dataset_teste/{0,1,2,3,4,5,6,7,8,9}
# [copie imagens .bin para cada subpasta]

sudo ./elm_app W_in_q.mif b_q.mif beta_q.mif <<EOF
3
dataset_teste

0
EOF

# O arquivo benchmark.csv é gerado automaticamente
cat benchmark.csv
```

### Verificação do CSV gerado

```bash
# Conta acertos e verifica formato
python3 -c "
import csv
with open('benchmark.csv') as f:
    rows = list(csv.DictReader(f))
acertos = sum(int(r['acerto']) for r in rows)
print(f'Total: {len(rows)}, Acertos: {acertos}, Acurácia: {100*acertos/len(rows):.1f}%')
"
```

### Teste do Modo 2 — VGA + mouse

```bash
sudo ./elm_app W_in_q.mif b_q.mif beta_q.mif /dev/input/event0
# Selecione opção 2, desenhe um dígito, clique com o botão do meio
# Verifique o resultado no terminal
# Pressione ENTER para voltar ao menu
```

---

## Resultados de Acurácia e Desempenho

> Os resultados abaixo foram obtidos com o dataset de validação disponível no repositório (150 imagens, 15 por dígito), compilado com `-O0`, na DE1-SoC com o FPGA programado com o co-processador ELM do Marco 1.

### Acurácia geral

| Métrica | Valor |
|---------|-------|
| Imagens processadas | 150 |
| Acertos | 120 |
| **Acurácia geral** | **80,00%** |

### Acurácia por dígito

| Dígito | Acertos | Total | Acurácia |
|--------|---------|-------|----------|
| 0 | 15 | 15 | 100,0% |
| 1 | 15 | 15 | 100,0% |
| 2 | 10 | 15 | 66,7% |
| 3 | 11 | 15 | 73,3% |
| 4 | 14 | 15 | 93,3% |
| 5 | 9 | 15 | 60,0% |
| 6 | 12 | 15 | 80,0% |
| 7 | 13 | 15 | 86,7% |
| 8 | 11 | 15 | 73,3% |
| 9 | 10 | 15 | 66,7% |

### Desempenho

| Métrica | Valor |
|---------|-------|
| Latência média | 16.778,9 µs (~16,8 ms) |
| Desvio padrão | 45,8 µs |
| Latência mínima | 16.739 µs |
| Latência máxima | 17.282 µs |
| **Throughput** | **59,6 imagens/s** |

> **Nota sobre reprodutibilidade:** para comparar com estes números, use o mesmo conjunto de 150 imagens `.bin` do repositório e o mesmo arquivo `.sof` do FPGA. Versões diferentes do co-processador Verilog podem produzir latências e acurácias distintas.

---

## Análise dos Resultados e Gargalos

### Latência: o gargalo principal é o workaround da inferência fantasma

A latência medida de ~16,8 ms **não representa o tempo de computação da ELM no FPGA**. O principal determinante é o `usleep(20000)` (20 ms de espera fixa) inserido na sequência de inferência para contornar o bug da primeira execução após reset:

```c
// elm_comum.c — inferir_digito()
elm_carregar_imagem((uint8_t *)img);
elm_disparar_inferencia();
usleep(20000);   // ← este sleep domina a latência total
elm_carregar_imagem((uint8_t *)img);
elm_disparar_inferencia();
```

O desvio padrão baixo (45,8 µs sobre uma média de 16.778 µs, ou ~0,3%) confirma que a latência é quase constante — determinada pelo sleep fixo, e não por variações de carga computacional.

**Melhoria possível:** corrigir o bug no RTL Verilog que causa o DONE espúrio na primeira inferência eliminaria a necessidade da inferência fantasma e do sleep, reduzindo a latência potencialmente para a faixa de centenas de microssegundos e aumentando o throughput de ~60 para `[PREENCHER: valor esperado após correção]` img/s.

### Acurácia: dígitos problemáticos são 5, 2, 9 e 3

Os dígitos com pior desempenho foram 5 (60,0%), 2 (66,7%), 9 (66,7%) e 3 (73,3%). Possíveis causas:

- **Confusão morfológica:** dígitos 5 e 6, 3 e 8, 2 e 7 compartilham curvaturas semelhantes no espaço de características da ELM com ativação tanh.
- **Dataset de teste pequeno (15 imagens/dígito):** a variância amostral é alta. Um dígito errado representa já 6,7% de queda de acurácia na classe.
- **Quantização Q4.12:** a representação de 16 bits pode introduzir erros de arredondamento que afetam mais as classes com margens de decisão menores.

`[PREENCHER: se disponível, inspecionar quais imagens específicas do dígito 5 foram erradas e descrever o padrão de confusão — ex.: "o modelo confundiu 5 com 6 em X dos Y erros"]`

### Acurácia: dígitos 0 e 1 com 100%

Os dígitos 0 e 1 atingiram 100% de acurácia, o que é esperado pela separabilidade elevada dessas classes no espaço de entrada (morfologia muito distinta dos demais dígitos).

### Dataset de validação limitado

O benchmark foi executado sobre 150 imagens (15 por dígito). Isso é suficiente para validação funcional, mas insuficiente para estimar a acurácia real do modelo com confiança estatística. Um dataset de 1.000 imagens (100 por dígito) daria intervalos de confiança mais estreitos.

`[PREENCHER: se o benchmark foi rodado sobre um dataset maior, substitua os dados da tabela acima pelos valores reais]`

### Throughput: 59,6 img/s vs. capacidade do hardware

O throughput de 59,6 img/s é limitado artificialmente pelo `usleep(20000)`. O hardware em si processa cada inferência em `[PREENCHER: tempo real de processamento do FPGA, observável pelo tempo entre disparo e DONE sem o sleep]` µs. Sem o workaround, o throughput teórico seria de `[PREENCHER]` img/s.

### Compilação com `-O0`

Os benchmarks foram coletados com `-O0` (sem otimizações de compilador). Isso aumenta levemente o overhead do HPS (loops de carga dos pesos, handshake Assembly), mas não afeta significativamente a latência total já dominada pelo sleep de 20 ms.

---

## Troubleshooting

### Erro: "Erro abrindo /dev/mem"
Execute com `sudo`. O driver requer acesso a `/dev/mem` para o mmap da região HPS-to-FPGA.

### Erro: "mmap do VGA falhou"
Verifique se o IP-Core VGA está mapeado no endereço esperado (`0xFF200000`). Recompile o projeto Quartus se o mapa de endereços foi alterado.

### Erro: "Erro abrindo /dev/input/eventX"
```bash
ls -la /dev/input/
cat /proc/bus/input/devices | grep -A5 "Mouse"
```
Use o dispositivo correto e execute com `sudo`.

### Timeout na inferência (STATUS nunca sobe DONE)
1. Verifique se o FPGA está programado com o `.sof` correto
2. Execute o Modo 4 (recarregar pesos) — o co-processador pode estar em estado inválido
3. Reinicie a aplicação para executar `elm_init()` novamente

### Benchmark não encontra imagens
```bash
# Verifique a estrutura:
find dataset/ -name "*.bin" | head -20
# Esperado: dataset/0/img.bin, dataset/1/img.bin, ...
```

### Resultado sempre igual (suspeita de DONE espúrio)
Se o co-processador retorna DONE antes do processamento real, a inferência fantasma não foi suficiente para "limpar" o estado. Aumente o `usleep` em `elm_comum.c` ou corrija o bug no RTL.

---

**Última atualização:** 2026-06-18
**Versão:** Marco 3 (Aplicação Unificada)
