# Testes do probe no PC (gcc + libjpeg-turbo): o mesmo código, com tests/host/shim
# no lugar da libctru. O MVD é falso e o Y2R não existe; o que se confere é a
# leitura dos dados, a divisão em NALs, o marcador, o decode JPEG e a rede.
#
#   make check                      jpeg + mvd
#   ./run_net.sh ../../../PSP-Stream   rede contra o servidor de verdade

SRC := ../../probe/source
CFLAGS := -O2 -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-format-zero-length -Wno-format \
          -Ishim -I$(SRC)
OBJS := main.c common.c fake_mvd.c $(SRC)/data.c $(SRC)/image.c $(SRC)/jpeg_test.c $(SRC)/mvd_test.c $(SRC)/net_test.c

probe_host: $(OBJS) shim/3ds.h $(wildcard $(SRC)/*.h)
	$(CC) $(CFLAGS) -o $@ $(OBJS) -ljpeg

romfs\::
	ln -sfn ../../probe/romfs 'romfs:'

check: probe_host romfs\:
	./probe_host mvd
	./probe_host jpeg | tee jpeg.out
	@! grep -q "problems\|err\|missing" jpeg.out || (echo "JPEG test failed"; exit 1)
	@test $$(grep -c "_q" jpeg.out) -ge 8 || (echo "JPEG sets missing"; exit 1)

clean:
	rm -f probe_host jpeg.out net.out 'romfs:'
	rm -rf 'sdmc:'

.PHONY: check clean romfs\:
