.PHONY: test test-ui test-partner simulator clean

CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic

UI_TEST_BIN := build/test_ui_core

test: test-ui test-partner

test-ui:
	mkdir -p build
	$(CC) $(CFLAGS) \
		-Ifirmware/components/pj_ui/include \
		firmware/components/pj_ui/pj_ui.c \
		tests/ui/test_ui_core.c \
		-o $(UI_TEST_BIN)
	$(UI_TEST_BIN)

test-partner:
	cd partner && PYTHONPATH=src python -m unittest discover -s ../tests/partner -p 'test_*.py'

simulator:
	cd simulator && python3 -m http.server 8765

clean:
	rm -rf build firmware/build partner/build partner/dist partner/*.egg-info

