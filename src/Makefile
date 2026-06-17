# ============================================================
# Makefile do projeto ELM (DE1-SoC)
#
# Alvos:
#   make                      # compila elm_app e golden_test (nativo)
#   make CROSS=1              # cross-compila (arm-linux-gnueabihf)
#   make elm_app              # só a aplicação unificada
#   make golden_test          # só a bateria de testes do Marco 2
#   make run                  # roda a aplicação unificada
#   make clean                # remove binários e objetos
# ============================================================

# Toolchain ------------------------------------------------------
ifeq ($(CROSS),1)
    CC  = arm-linux-gnueabihf-gcc
    AS  = arm-linux-gnueabihf-as
else
    CC  = gcc
    AS  = as
endif

# Flags ----------------------------------------------------------
# -O0 enquanto debugamos; troque para -O2 quando for medir desempenho.
CFLAGS  = -Wall -Wextra -O0 -g -std=gnu99
# -lm: sqrt etc.  -lrt: clock_gettime em glibc antiga (< 2.17)
LDFLAGS = -lm -lrt

# Arquivos -------------------------------------------------------
ASM_OBJ      = elm_driver.o

APP_OBJ      = elm_app.o elm_comum.o conversor_img.o elm.o $(ASM_OBJ)
TEST_OBJ     = golden_test.o elm.o $(ASM_OBJ)

TARGETS      = elm_app golden_test

# Regras ---------------------------------------------------------
all: $(TARGETS)

elm_app: $(APP_OBJ)
	$(CC) $(APP_OBJ) -o $@ $(LDFLAGS)
	@echo ""
	@echo "  Build OK: $@"
	@echo "  Para rodar: sudo ./$@ <W.mif> <b.mif> <beta.mif> [/dev/input/eventX]"
	@echo "  (precisa de sudo porque o driver abre /dev/mem)"

golden_test: $(TEST_OBJ)
	$(CC) $(TEST_OBJ) -o $@ $(LDFLAGS)
	@echo ""
	@echo "  Build OK: $@"
	@echo "  Para rodar: sudo ./$@ <img.bin> <label> <W.mif> <b.mif> <beta.mif>"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.s
	$(AS) $< -o $@

# Conveniência: assume nomes padrão
run: elm_app
	sudo ./elm_app W_in_q.mif b_q.mif beta_q.mif /dev/input/event0

clean:
	rm -f *.o $(TARGETS) latencias.csv benchmark.csv

# Dependências manuais -------------------------------------------
elm_app.o:       elm_app.c elm_comum.h conversor_img.h elm.h
elm_comum.o:     elm_comum.c elm_comum.h elm.h
conversor_img.o: conversor_img.c conversor_img.h elm_comum.h stb_image.h
golden_test.o: golden_test.c golden_test.h elm.h
elm.o:         elm.c elm.h

.PHONY: all clean run
