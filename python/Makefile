PYTHON ?= python3
KILIX_STATE_DIR ?= ..
KILIX_STATE_LIBRARY := $(abspath $(KILIX_STATE_DIR)/build/libkilix-state.so)
PYTHON_SOURCES := $(wildcard src/kilix_state/*.py)
TEST_SOURCES := $(wildcard tests/*.py)

.DEFAULT_GOAL := all

all: native compile

native:
	$(MAKE) -C $(KILIX_STATE_DIR) all

upstream-test:
	$(MAKE) -C $(KILIX_STATE_DIR) test

compile:
	$(PYTHON) -m py_compile $(PYTHON_SOURCES) $(TEST_SOURCES) examples/basic.py

test: native
	PYTHONPATH=src KILIX_STATE_LIBRARY=$(KILIX_STATE_LIBRARY) \
		$(PYTHON) -m unittest discover -s tests -v

check: upstream-test compile test

example: native
	PYTHONPATH=src KILIX_STATE_LIBRARY=$(KILIX_STATE_LIBRARY) \
		$(PYTHON) examples/basic.py

wheel: native
	$(PYTHON) -m pip wheel --no-deps --no-build-isolation --wheel-dir dist .

clean:
	rm -rf build dist src/kilix_state_py.egg-info \
		src/kilix_state/__pycache__ tests/__pycache__ examples/__pycache__

.PHONY: all native upstream-test compile test check example wheel clean
