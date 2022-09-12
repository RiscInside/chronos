CFLAGS = -Og -Wall -Werror -Wextra

all: build/libchronosrt.a

.PHONY: clean regen-libbpf build-test test

build/keep:
	mkdir -p build
	touch build/keep

regen-libbpf:
	$(RM) -r libbpf
	git clone https://github.com/libbpf/libbpf
	make -C libbpf/src BUILD_STATIC_ONLY=1 OBJDIR=../build/libbpf DESTDIR=../build INCLUDEDIR= LIBDIR= UAPIDIR= install

libbpf/build/libbpf.a:
	git clone https://github.com/libbpf/libbpf
	make -C libbpf/src BUILD_STATIC_ONLY=1 OBJDIR=../build/libbpf DESTDIR=../build INCLUDEDIR= LIBDIR= UAPIDIR= install

build/vmlinux.h: build/keep
	bpftool btf dump file /sys/kernel/btf/vmlinux format c > build/vmlinux.h

build/trace_switch.bpf.o: build/keep build/vmlinux.h src/trace_switch.bpf.c src/trace_switch.h
	clang -g -c -target bpf -D__TARGET_ARCH_x86_64 -I build -I src -o build/trace_switch.bpf.o src/trace_switch.bpf.c $(CFLAGS)

build/trace_switch.skel.h: build/keep build/trace_switch.bpf.o
	bpftool gen skeleton build/trace_switch.bpf.o > build/trace_switch.skel.h

build/ns.o: src/ns.c src/ns.h src/log.h
	$(CC) -Iinclude -Isrc -g -c -o $@ $< $(CFLAGS)

build/cpuset.o: src/cpuset.c include/chronos/cpuset.h src/log.h
	$(CC) -Iinclude -Isrc -g -c -o $@ $< $(CFLAGS)

build/log.o: src/log.c src/log.h
	$(CC) -Iinclude -Isrc -g -c -o $@ $< $(CFLAGS)

build/vruntime.o: src/vruntime.c src/vruntime.h src/log.h
	$(CC) -Iinclude -Isrc -g -c -o $@ $< $(CFLAGS)

build/chronosrt.o: src/chronosrt.c build/keep build/trace_switch.skel.h include/chronos/chronosrt.h include/chronos/cpuset.h src/ns.h src/vruntime.h src/log.h src/pairing_heap.h
	$(CC) -Ibuild -Iinclude -Isrc -g -c -o $@ $< $(CFLAGS)

build/test_helpers.o: src/test_helpers.c build/keep src/ns.h include/chronos/chronosrt.h include/chronos/cpuset.h
	$(CC) -Iinclude -Isrc -g -c -o $@ $< $(CFLAGS)

build/libchronosrt.a: build/chronosrt.o build/ns.o build/cpuset.o build/vruntime.o build/log.o
	$(AR) rc $@ $^

build/%: tests/%.c build/keep build/test_helpers.o build/libchronosrt.a libbpf/build/libbpf.a
	$(CC) -o $@ -g -Isrc -Iinclude $< build/test_helpers.o build/libchronosrt.a libbpf/build/libbpf.a -lelf -lz -latomic

build-test: \
	build/rt_test_helpers \
	build/rt_vruntime \
	build/rt_plain \
	build/rt_two_threads \
	build/rt_scaling \
	build/rt_yield \
	build/rt_mt_stress_test \
	build/rt_nested_barriers \
	build/rt_barrier_deadlock

test: build-test
	@echo "* Test helpers test"
	@./build/rt_test_helpers
	@echo "* Vruntime calculation test"
	@./build/rt_vruntime
	@echo "* Single-threaded runtime test"
	@./build/rt_plain
	@echo "* Multi-threaded runtime test"
	@./build/rt_two_threads
	@echo "* Multi-core multi-threaded runtime test"
	@./build/rt_two_threads -C 0-2
	@echo "* Yield test"
	@./build/rt_yield
	@echo "* Scaling test"
	@./build/rt_scaling
	@echo "* Multi-core scaling test"
	@./build/rt_scaling -C 0-2
	@echo "* Nested barrier test (singlecore)"
	@./build/rt_nested_barriers
	@echo "* Nested barrier test (multicore)"
	@./build/rt_nested_barriers -C 0-2
	@echo "* Barrier deadlock test (singlecore)"
	@./build/rt_barrier_deadlock
	@echo "* Barrier deadlock test (multicore)"
	@./build/rt_barrier_deadlock -C 0-2
	@echo "* Scheduler stress-test (singlecore)"
	@./build/rt_mt_stress_test
	@echo "* Scheduler stress-test (multicore)"
	@./build/rt_mt_stress_test -C 0-2

clean:
	$(RM) -r build
