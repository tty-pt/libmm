INSTALL_BIN := mm
all := libmm mm

libmm-obj-y := src/mm_ctx.o src/mm_fusion.o src/mm_code.o src/mm_session.o src/mm_mcp.o src/mm_project.o src/mm_distill.o

LDLIBS-libmm := -L../libcorm/lib -L../libstele/lib -lcorm -lstele -lm
LDLIBS-mm := -L./lib -L../libcorm/lib -L../libstele/lib -lmm -lcorm -lstele -lm -Wl,-rpath,'$$ORIGIN/../lib'
CFLAGS-BIN-Windows :=

WASM_SRCS := npm/wasm/mm_wasm.c
WASM_EXPORTS := mm_wasm_record mm_wasm_recall_json mm_wasm_review
WASM_EXTRA_CFLAGS := -Iinclude -I../libcorm/include -I../libstele/include

include ../mk/include.mk
LDLIBS-alpine :=

CFLAGS := -I./include -I../libcorm/include -I../libstele/include -O2 ${CFLAGS} $(EXTRA_CFLAGS)
LDFLAGS := -L./lib -L../libcorm/lib -L../libstele/lib ${LDFLAGS}

TEST_BINS := bin/test_payload${EXE} bin/test_fusion${EXE} bin/test_code${EXE} bin/test_tiers_cli_mcp${EXE} bin/test_project${EXE} bin/test_distill${EXE} bin/test_metrics${EXE} bin/test_token_decode${EXE} bin/test_ren_mm${EXE} bin/test_federation${EXE} bin/test_ipc_invalidation${EXE} bin/test_telemetry${EXE}
TEST_LIB_PATH := ./lib:../libcorm/lib:../libstele/lib:../libjoint/lib:../liblemma/lib:../libsepal/lib:../libislet/lib:../axil-auren/lib:/opt/homebrew/lib:/usr/local/lib:/usr/lib
TEST_ENV := LD_LIBRARY_PATH=${TEST_LIB_PATH}:$$LD_LIBRARY_PATH DYLD_LIBRARY_PATH=${TEST_LIB_PATH}:$$DYLD_LIBRARY_PATH CORM_AXIS_PATH=../libjoint/lib:../liblemma/lib:../libsepal/lib:../libislet/lib:/opt/homebrew/lib:/usr/local/lib:/usr/lib

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

bin/test_project${EXE}: tests/test_project.c lib/libmm.${SO}
	@mkdir -p bin
	${cc} ${CFLAGS} -o $@ tests/test_project.c ${LDFLAGS} ${LDLIBS-mm}

bin/test_distill${EXE}: tests/test_distill.c lib/libmm.${SO}
	@mkdir -p bin
	${cc} ${CFLAGS} -o $@ tests/test_distill.c ${LDFLAGS} ${LDLIBS-mm}

bin/test_metrics${EXE}: tests/test_metrics.c lib/libmm.${SO}
	@mkdir -p bin
	${cc} ${CFLAGS} -o $@ tests/test_metrics.c ${LDFLAGS} ${LDLIBS-mm}

bin/test_token_decode${EXE}: tests/test_token_decode.c lib/libmm.${SO}
	@mkdir -p bin
	${cc} ${CFLAGS} -I../liblemma/include -o $@ tests/test_token_decode.c ${LDFLAGS} ${LDLIBS-mm} -L../liblemma/lib -llemma

bin/test_ren_mm${EXE}: tests/test_ren_mm.c mods/ren-mm/ren-mm.c lib/libmm.${SO}
	@mkdir -p bin
	${cc} ${CFLAGS} -I../axil-auren/include -I../libxylem/include -o $@ tests/test_ren_mm.c ${LDFLAGS} ${LDLIBS-mm} -L../axil-auren/lib -lren -L../liblemma/lib -llemma -ljson-c

bin/test_federation${EXE}: tests/test_federation.c lib/libmm.${SO}
	@mkdir -p bin
	${cc} ${CFLAGS} -o $@ tests/test_federation.c ${LDFLAGS} ${LDLIBS-mm}

bin/test_ipc_invalidation${EXE}: tests/test_ipc_invalidation.c lib/libmm.${SO}
	@mkdir -p bin
	${cc} ${CFLAGS} -o $@ tests/test_ipc_invalidation.c ${LDFLAGS} ${LDLIBS-mm}

bin/test_telemetry${EXE}: tests/test_telemetry.c lib/libmm.${SO}
	@mkdir -p bin
	${cc} ${CFLAGS} -o $@ tests/test_telemetry.c ${LDFLAGS} ${LDLIBS-mm}

mods/ren-mm/ren-mm.so: mods/ren-mm/ren-mm.c lib/libmm.${SO}
	@mkdir -p mods/ren-mm
	${cc} ${CFLAGS} -I../axil-auren/include -I../libxylem/include -fPIC -shared -o $@ mods/ren-mm/ren-mm.c ${LDFLAGS} -L./lib -lmm -L../axil-auren/lib -lren -ljson-c -Wl,-rpath,'$$ORIGIN/../../lib'

test: all ${TEST_BINS}
	${TEST_ENV} ./bin/test_payload${EXE}
	${TEST_ENV} ./bin/test_fusion${EXE}
	${TEST_ENV} ./bin/test_code${EXE}
	${TEST_ENV} ./bin/test_tiers_cli_mcp${EXE}
	${TEST_ENV} ./bin/test_project${EXE}
	${TEST_ENV} ./bin/test_distill${EXE}
	${TEST_ENV} ./bin/test_metrics${EXE}
	${TEST_ENV} ./bin/test_token_decode${EXE}
	${TEST_ENV} ./bin/test_ren_mm${EXE}
	${TEST_ENV} ./bin/test_federation${EXE}
	${TEST_ENV} ./bin/test_ipc_invalidation${EXE}
	${TEST_ENV} ./bin/test_telemetry${EXE}
	${TEST_ENV} sh ./tests/test_cli_mcp.sh

bench: all bin/test_metrics${EXE}
	@echo "=== libmm Microsecond Latency Benchmark ==="
	${TEST_ENV} ./bin/test_metrics${EXE}
	${TEST_ENV} ./bin/mm status

${DESTDIR}${PREFIX}/lib/pkgconfig/mm.pc: mm.pc
	install -d ${DESTDIR}${PREFIX}/lib/pkgconfig
	install -m 644 mm.pc $@

install: ${DESTDIR}${PREFIX}/lib/pkgconfig/mm.pc install-ren-mod

install-ren-mod: mods/ren-mm/ren-mm.so
	install -d ${DESTDIR}${PREFIX}/lib/ren
	@rm -f ${DESTDIR}${PREFIX}/lib/ren/mod_mm.so ${DESTDIR}${PREFIX}/lib/mod_mm.so
	install -m 755 mods/ren-mm/ren-mm.so ${DESTDIR}${PREFIX}/lib/ren/ren-mm.so

clean-extra::
	rm -rf bin lib src/*.o src/*.d

objects-set.mk: Makefile

# Optional Node.js & Deno N-API bindings
NPM ?= npm

sync-npm-headers:
	mkdir -p npm/include/ttypt
	cp include/ttypt/mm.h npm/include/ttypt/mm.h
	cp mm.pc npm/mm.pc

npm-build: all sync-npm-headers
	@if command -v $(NPM) >/dev/null 2>&1; then \
		(cd npm && $(NPM) run build:native); \
	else \
		echo "Skipping npm build — $(NPM) not found"; \
	fi

npm-test: npm-build
	@if command -v $(NPM) >/dev/null 2>&1; then \
		(cd npm && ${TEST_ENV} $(NPM) test); \
	fi

.PHONY: all test install sync-npm-headers npm-build npm-test

