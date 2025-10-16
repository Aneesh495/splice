.PHONY: bootstrap build test test-differential test-pty test-stress test-faults fuzz benchmark demo acceptance verify clean

bootstrap:
	python3 tools/bootstrap.py

build:
	cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
	cmake --build build

test: build
	ctest --test-dir build --output-on-failure

test-differential: build
	python3 tools/differential.py --executable ./build/splice --count 100

test-pty: build
	ctest --test-dir build --output-on-failure -R 'splice_pty_cases'

test-stress: build
	python3 tools/stress.py --executable ./build/splice --cycles 1000

test-faults: build
	ctest --test-dir build --output-on-failure -R 'fault|runtime_fault'

fuzz: build
	python3 tools/fuzz.py --executable ./build/splice --cases 1000 < /dev/null

benchmark: build
	python3 tools/benchmark.py --executable ./build/splice --repetitions 10

demo: build
	python3 tools/demo.py --executable ./build/splice < /dev/null

acceptance: build
	python3 tools/acceptance.py --executable ./build/splice

verify:
	python3 tools/verify.py

clean:
	cmake --build build --target clean
