# Marco 3 - Co-Processador ELM no DE1-SoC

Aplicação unificada para o **co-processador ELM (Extreme Learning Machine)** implementado no FPGA da placa DE1-SoC (ARM + FPGA), com suporte a três modos de operação: inferência a partir de arquivo, desenho interativo com mouse e validação/benchmark sobre datasets.

---

## Índice

- [Visão Geral](#visão-geral)
- [Pré-requisitos](#pré-requisitos)
- [Estrutura do Projeto](#estrutura-do-projeto)
- [Compilação](#compilação)
- [Uso](#uso)
- [Modos de Operação](#modos-de-operação)
- [Arquivos de Entrada](#arquivos-de-entrada)
- [Saída e Resultados](#saída-e-resultados)
- [Arquitetura e Implementação](#arquitetura-e-implementação)
- [Troubleshooting](#troubleshooting)

---

## Visão Geral

Este projeto implementa uma **aplicação de reconhecimento de dígitos (0-9)** usando a rede neural ELM, executando a inferência no co-processador FPGA do DE1-SoC para máxima performance. A aplicação oferece:

- **Modo 1**: Classificação de imagens carregadas do disco (PNG, JPG, BMP, BIN)
- **Modo 2**: Classificação de dígitos desenhados interativamente no VGA com mouse
- **Modo 3**: Validação automática sobre datasets com cálculo de acurácia, latência e throughput
- **Recarregamento de pesos**: Capacidade de recarregar pesos do hardware em tempo de execução

A aplicação foi desenvolvida em **C** com conversão de imagens integrada (usando `stb_image.h` — sem dependências externas de ImageMagick).

---

## Pré-requisitos

### Hardware
- Placa **Altera DE1-SoC** com FPGA e ARM HPS
- Display VGA conectado
- Mouse USB (opcional, pode ser informado em tempo de execução)

### Software (no ARM Linux)
- **Toolchain ARM**: `arm-linux-gnueabihf-gcc` e `arm-linux-gnueabihf-as` (para compilação cruzada)
- **Biblioteca matemática**: `libm` (geralmente pré-instalada)
- **Biblioteca de tempo real**: `librt` (para `clock_gettime`)
- **Acesso root**: `sudo` é necessário para acessar `/dev/mem` e `/dev/input/event*`

### Arquivos de Pesos
O co-processador requer três arquivos em formato MIF (Memory Initialization File):
- `W_in_q.mif` — Matriz de pesos de entrada (quantizada em Q4.12)
- `b_q.mif` — Vetor de bias (quantizado em Q4.12)
- `beta_q.mif` — Vetor de saída beta (quantizado em Q4.12)

Estes arquivos são fornecidos no repositório em `src/`.

---

## Estrutura do Projeto

```
Marco_3-CoProcessador_ELM-DE1-SoC/
├── README.md                    # Este arquivo
├── Problema-MI-SD-2026-1(V02).pdf  # Especificação do projeto
├── imagens/                     # Diretório para armazenar imagens (vazio)
└── src/
    ├── Makefile                 # Script de compilação
    ├── elm_app.c                # Aplicação principal unificada
    ├── elm.c / elm.h            # API do co-processador
    ├── elm_comum.c / elm_comum.h  # Funções comuns
    ├── elm_driver.s             # Driver de baixo nível (assembly)
    ├── conversor_img.c / conversor_img.h  # Conversão de imagens
    ├── golden_test.c / golden_test.h      # Suite de testes (Marco 2)
    ├── stb_image.h              # Biblioteca de carregamento de imagens (público)
    ├── W_in_q.mif               # Pesos de entrada (~1.3 MB)
    ├── b_q.mif                  # Bias (~1.5 KB)
    ├── beta_q.mif               # Beta (~15 KB)
    ├── benchmark.csv            # Resultados do último benchmark
    ├── *.bin                    # Arquivos de teste (784 bytes cada)
    └── *.png                    # Versões PNG dos testes
```

---

## Compilação

### Compilação Nativa (no ARM)
```bash
cd src
make                    # Compila elm_app e golden_test
make elm_app            # Apenas a aplicação unificada
make clean              # Remove binários e objetos
```

### Compilação Cruzada (no host x86_64)
```bash
cd src
make CROSS=1            # Usa arm-linux-gnueabihf-gcc
make CROSS=1 elm_app    # Apenas a aplicação
```

### Flags de Compilação
- **CFLAGS**: `-Wall -Wextra -O0 -g -std=gnu99` (debug; mude para `-O2` para otimização)
- **LDFLAGS**: `-lm -lrt` (matemática e relógio do sistema)

---

## Uso

### Execução Básica
```bash
cd src
sudo ./elm_app W_in_q.mif b_q.mif beta_q.mif [/dev/input/eventX]
```

**Argumentos obrigatórios:**
- `W_in_q.mif` — Arquivo de pesos de entrada
- `b_q.mif` — Arquivo de bias
- `beta_q.mif` — Arquivo de beta (vetor de saída)

**Argumento opcional:**
- `/dev/input/eventX` — Dispositivo do mouse (ex.: `/dev/input/event0`)
  - Se omitido, será solicitado ao entrar no **Modo 2 (desenho)**

### Exemplo Completo
```bash
cd /home/user/Marco_3-CoProcessador_ELM-DE1-SoC/src
sudo ./elm_app W_in_q.mif b_q.mif beta_q.mif /dev/input/event0
```

---

## 🎮 Modos de Operação

Ao iniciar, a aplicação exibe um **menu interativo** com 5 opções:

### **Modo 1: Inferência a partir de Arquivo**
**Descrição:** Carrega uma imagem do disco e a classifica.

**Formatos suportados:**
- `.png`, `.jpg`, `.jpeg`, `.bmp` — Convertidos automaticamente para MNIST (784B, 28×28 pixels)
- `.bin` — Arquivo raw de 784 bytes (usado diretamente)

**Processo:**
1. Escolha a opção `1` no menu
2. Informe o caminho da imagem
3. A aplicação:
   - Carrega e redimensiona a imagem (se necessário)
   - Converte para escala de cinza e normaliza para MNIST
   - Salva a versão `.bin` ao lado do arquivo original (se convertida)
   - Dispara a inferência no co-processador
   - Exibe o dígito predito e latência

**Saída esperada:**
```
==================================
>>> DÍGITO CLASSIFICADO: 5 <<<
    (latência: 245 us)
==================================
```

---

### **Modo 2: Inferência por Desenho (VGA + Mouse)**
**Descrição:** Desenha um dígito na tela e o classifica em tempo real.

**Interface de desenho:**
- **Canvas:** Caixa 224×224 pixels (downsampled para 28×28 no MNIST)
- **Cores:**
  - Fundo: preto
  - Traço: branco
  - Moldura: azul
  - Cursor (crosshair): vermelho

**Controles:**
| Ação | Resultado |
|------|-----------|
| Botão esquerdo + movimento | Desenhar |
| Botão direito | Limpar a tela |
| Botão do meio | Classificar o dígito |
| **ENTER** (no terminal) | Voltar ao menu |

**Processo:**
1. Escolha a opção `2` no menu
2. Informe o dispositivo do mouse (ou use o argumento CLI)
3. A tela VGA é inicializada e limpa
4. Desenhe um dígito com o botão esquerdo do mouse
5. Clique com o botão do meio para classificar
6. Veja o resultado no terminal
7. Pressione **ENTER** no terminal para sair

---

### **Modo 3: Validação / Benchmark**
**Descrição:** Valida a rede neural contra um dataset estruturado.

**Estrutura esperada do dataset:**
```
<pasta_raiz>/
├── 0/
│   ├── img1.bin
│   ├── img2.bin
│   └── ...
├── 1/
│   ├── img1.bin
│   └── ...
├── ...
└── 9/
    ├── imgN.bin
    └── ...
```

**Processo:**
1. Escolha a opção `3` no menu
2. Informe o caminho da pasta raiz
3. A aplicação:
   - Varre as subpastas 0..9 recursivamente
   - Conta quantas imagens `.bin` em cada classe
   - Processa todas as imagens e coleta estatísticas
   - Exibe relatório de acurácia por classe
   - Salva resultados detalhados em `benchmark.csv`

**Saída esperada:**
```
╔══════════════════════════════════════════════╗
║          RESULTADOS DO BENCHMARK             ║
╠══════════════════════════════════════════════╣
║  Imagens processadas :   1000               ║
║  Acertos             :    972               ║
║  Acurácia            :  97.20 %             ║
╠══════════════════════════════════════════════╣
║  Latência média      :   245.3 us          ║
║  Desvio padrão       :    12.4 us          ║
║  Latência mínima     :    235 us            ║
║  Latência máxima     :    289 us            ║
║  Throughput          : 4081.63 img/s        ║
╠══════════════════════════════════════════════╣
║  Acurácia por classe:                        ║
║    Dígito 0 :  100/ 100  (100.00 %)        ║
║    Dígito 1 :   98/ 100  ( 98.00 %)        ║
│ ...                                          │
║  Dígito 9 :  95/ 100  ( 95.00 %)         ║
╚══════════════════════════════════════════════╝

[elm_app] Resultados individuais salvos em 'benchmark.csv'.
```

---

### **Modo 4: Recarregar Pesos**
**Descrição:** Recarrega os pesos do hardware sem sair da aplicação.

**Uso:**
1. Escolha a opção `4` no menu
2. A aplicação recarrega os três arquivos de pesos no co-processador
3. Informe se a operação foi bem-sucedida ou se houve erro
4. Volta ao menu

---

### **Modo 0: Sair**
Encerra a aplicação e libera o acesso a `/dev/mem`.

---

## Arquivos de Entrada

### Imagens
- **Formato PNG/JPG/BMP:** Automaticamente convertidas para 28×28 pixels em escala de cinza, normalizadas para a faixa [0, 255]
- **Formato BIN:** Raw 784 bytes (28×28), esperado em [0, 255], sem cabeçalho
- **Exceção:** O Modo 2 captura desenhos e os converte automaticamente

### Pesos (MIF)
Arquivos de inicialização de memória com formato hexadecimal:
```
WIDTH=16;
DEPTH=1024;
ADDRESS_RADIX=HEX;
DATA_RADIX=HEX;
CONTENT
0 : 1A2B;
1 : 3C4D;
...
END;
```

Os pesos estão **quantizados em Q4.12** (ponto fixo de 16 bits com escala fixa de 2^-12).

---

## Saída e Resultados

### Modo 1 e 2: Resultado Individual
- Dígito predito (0-9)
- Latência em microsegundos

### Modo 3: Arquivo CSV
Arquivo `benchmark.csv` com coluna-delimitada:
```csv
arquivo,label_esperado,digito_predito,acerto,latencia_us
/data/0/img1.bin,0,0,1,243
/data/0/img2.bin,0,0,1,251
/data/1/img1.bin,1,1,1,238
...
```

---

## Arquitetura e Implementação

### Módulos Principais

#### `elm.h / elm.c`
**API estável do co-processador**
- `elm_init()` — Abre `/dev/mem` e mapeia o registrador HPS-to-FPGA
- `elm_carregar_imagem()` — Carrega 784 bytes de imagem
- `elm_carregar_pesos_raw()`, `elm_carregar_bias_raw()`, `elm_carregar_beta_raw()` — Carregam pesos
- `elm_disparar_inferencia()` — Pulsa o sinal de início
- `elm_obter_resultado()` — Poll com timeout para ler o resultado
- `elm_resetar_hardware()` — Reset do co-processador
- `elm_close()` — Libera `/dev/mem`

#### `elm_comum.h / elm_comum.c`
**Funções de suporte compartilhadas**
- Carregamento de arquivos MIF
- Carregamento de imagens BIN
- Conversão de imagens (integrada com `stb_image.h`)
- Impressão de preview ASCII de imagens
- Funções auxiliares

#### `conversor_img.h / conversor_img.c`
**Pipeline de conversão de imagens**
- Suporte PNG, JPG, BMP via `stb_image.h`
- Redimensionamento para 28×28
- Normalização para [0, 255]
- Salva versões BIN para reutilização

#### `elm_driver.s`
**Driver de baixo nível em assembly ARM**
- Manipulação de registradores do co-processador FPGA
- Acesso à memória compartilhada HPS-FPGA

#### `elm_app.c`
**Aplicação principal** (~27 KB)
- Menu interativo (SEÇÃO 4)
- Modo arquivo (SEÇÃO 2)
- Modo desenho VGA+mouse (SEÇÃO 1)
- Modo benchmark (SEÇÃO 3)
- Inicialização e limpeza

---

## Troubleshooting

### Erro: "Erro abrindo /dev/mem"
**Causa:** Permissões insuficientes.
**Solução:** Use `sudo`:
```bash
sudo ./elm_app W_in_q.mif b_q.mif beta_q.mif
```

### Erro: "Erro abrindo /dev/input/eventX"
**Causa:** Dispositivo do mouse inválido ou sem permissão.
**Solução:**
1. Verifique o dispositivo correto:
   ```bash
   ls -la /dev/input/
   ```
2. Use `sudo` ao executar
3. Ou informe o dispositivo correto na linha de comando

### Erro: "Arquivo não encontrado"
**Causa:** Caminho absoluto ou relativo incorreto.
**Solução:**
1. Use caminhos absolutos (ex.: `/home/user/imagens/digit.png`)
2. Ou navegue até o diretório correto antes de executar

### Timeout na Inferência
**Causa:** Co-processador não respondendo.
**Solução:**
1. Verifique se o FPGA foi programado corretamente
2. Recarregue os pesos (Modo 4)
3. Reinicie a aplicação

### Imagem Não é Carregada
**Causa:** Formato não suportado ou arquivo corrompido.
**Solução:**
1. Converta para PNG/JPG/BMP ou BIN manualmente
2. Valide que a imagem tem 28×28 e está em escala de cinza
3. Para BIN, verifique que tem exatamente 784 bytes

### Benchmark Não Encontra Imagens
**Causa:** Estrutura de pastas incorreta.
**Solução:**
```bash
# Crie a estrutura esperada:
mkdir -p dataset/{0,1,2,3,4,5,6,7,8,9}
# Coloque imagens .bin em cada subpasta
```

---

## Notas Importantes

1. **Quantização em Q4.12:** Todos os pesos são de 16 bits em ponto fixo Q4.12. A conversão é feita internamente pelo hardware.

2. **Normalização de Imagens:** Imagens carregadas são normalizadas para [0, 255] (8 bits unsigned).

3. **Latência Típica:** ~240-280 microsegundos por inferência (compilado com `-O0`; use `-O2` para benchmark).

4. **Taxa de Transferência:** ~4000-5000 imagens/segundo (dependendo do hardware).

5. **Memory-mapped I/O:** O co-processador é acessado via registradores de memória compartilhada HPS-FPGA, não há overhead de syscalls após `elm_init()`.

---

## Contato e Suporte

Para dúvidas ou problemas, consulte:
- Especificação do projeto: `Problema-MI-SD-2026-1(V02).pdf`
- Código-fonte comentado em `src/*.c` e `src/*.h`
- Documentação da placa DE1-SoC (Altera/Intel)

---

**Última atualização:** 2026-06-17  
**Versão:** Marco 3 (Aplicação Unificada)
