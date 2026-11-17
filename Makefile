INSTALL_BIN := mm
all := libmm mm

libmm-obj-y := src/mm_ctx.o src/mm_fusion.o src/mm_code.o src/mm_session.o src/mm_mcp.o

LDLIBS-libmm := -L../libcorm/lib -L../libstele/lib -lcorm -lstele -lm
LDLIBS-mm := -L./lib -L../libcorm/lib -L../libstele/lib -lmm -lcorm -lstele -lm

include ../mk/include.mk
LDLIBS-alpine :=

CFLAGS := -I./include -I../libcorm/include -I../libstele/include -O2 -Wno-format-truncation ${CFLAGS} $(EXTRA_CFLAGS)
LDFLAGS := -L./lib -L../libcorm/lib -L../libstele/lib ${LDFLAGS}

TEST_BINS := bin/test_payload${EXE} bin/test_fusion${EXE} bin/test_code${EXE} bin/test_tiers_cli_mcp${EXE}
TEST_ENV := LD_LIBRARY_PATH=./lib:../libcorm/lib:../libstele/lib:../libjoint/lib:../liblemma/lib:../libsepal/lib:../libislet/lib:$$LD_LIBRARY_PATH CORM_AXIS_PATH=../libjoint/lib:../liblemma/lib:../libsepal/lib:../libislet/lib

bin/test_payload${EXE}: tests/test_payload.c lib/libmm.${SO}
	@mkdir -p bin
	${cc} ${CFLAGS} -o $@ tests/test_payload.c ${LDFLAGS} ${LDLIBS-mm}

bin/test_fusion${EXE}: tests/test_fusion.c lib/libmm.${SO}
	@mkdir -p bin
	${cc} ${CFLAGS} -o $@ tests/test_fusion.c ${LDFLAGS} ${LDLIBS-mm}

bin/test_code${EXE}: tests/test_code.c lib/libmm.${SO}
	@mkdir -p bin
	${cc} ${CFLAGS} -o $@ tests/test_code.c ${LDFLAGS} ${LDLIBS-mm}

bin/test_tiers_cli_mcp${EXE}: tests/test_tiers_cli_mcp.c lib/libmm.${SO}
	@mkdir -p bin
	${cc} ${CFLAGS} -o $@ tests/test_tiers_cli_mcp.c ${LDFLAGS} ${LDLIBS-mm}

test: all ${TEST_BINS}
	${TEST_ENV} ./bin/test_payload${EXE}
	${TEST_ENV} ./bin/test_fusion${EXE}
	${TEST_ENV} ./bin/test_code${EXE}
	${TEST_ENV} ./bin/test_tiers_cli_mcp${EXE}
	sh ./tests/test_cli_mcp.sh

${DESTDIR}${PREFIX}/lib/pkgconfig/mm.pc: mm.pc
	install -d ${DESTDIR}${PREFIX}/lib/pkgconfig
	install -m 644 mm.pc $@

install: ${DESTDIR}${PREFIX}/lib/pkgconfig/mm.pc

clean-extra::
	rm -rf bin lib src/*.o src/*.d

objects-set.mk: Makefile

.PHONY: all test install
