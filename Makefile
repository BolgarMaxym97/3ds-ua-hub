SHELL := /bin/bash
VERSION := 1.2.0

# Python with fontTools and Pillow (requirements.txt): `make venv` once.
PY := .venv/bin/python
# devkitARM in docker, as 3ds-ua builds its installer.
DKA := docker run --rm --user "$$(id -u):$$(id -g)" -v "$$PWD:/w" -w /w devkitpro/devkitarm
# makerom (3DSGuy/Project_CTR) and bannertool (carstene1ns/3ds-bannertool) are not in the
# devkitARM image; by default the builds 3ds-ua already has.
MAKEROM ?= ../3ds-ua/tmp/tools/makerom/makerom
BANNERTOOL ?= ../3ds-ua/tmp/tools/bannertool.bin

FONT := app/assets/fonts/Nunito[wght].ttf
CIA := build/3ds-ua-hub-$(VERSION).cia

.PHONY: help venv fonts art app cia makerom test clean

help:  ## show this list
	@grep -E '^[a-z-]+:.*?##' $(MAKEFILE_LIST) | sed 's/:.*##/\t—/'

venv: ## Python for fonts and art -> .venv
	python3 -m venv .venv && $(PY) -m pip install -q -r requirements.txt

# Nunito (OFL) at 700 and 800, cut to the characters the app draws, as bcfnt for citro2d.
fonts: ## the UI fonts -> app/romfs/gfx (needs .venv + docker)
	@mkdir -p build/fonts app/romfs/gfx
	for w in 700 800; do $(PY) -m fontTools.varLib.instancer "$(FONT)" wght=$$w -q -o build/fonts/Nunito-$$w.ttf || exit 1; done
	$(PY) -c "cps = [*range(0x20, 0x7F), *range(0xA0, 0x100), *range(0x400, 0x460), 0x490, 0x491, 0x2BC, 0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x201E, 0x2026, 0x2116, 0x2192, 0x2248, 0x2605]; open('build/fonts/charset.txt', 'w').write(' '.join(map(str, cps)))"
	$(DKA) sh -c 'for w in 700 800; do /opt/devkitpro/tools/bin/mkbcfnt -s 24 -w build/fonts/charset.txt -o app/romfs/gfx/nunito_$$w.bcfnt build/fonts/Nunito-$$w.ttf || exit 1; done'

art: ## the HOME Menu icon, banner and its sound -> build/art (after fonts)
	$(PY) tools/art.py build/fonts/Nunito-800.ttf build/art

app: ## the app -> app/3ds-ua-hub.3dsx (after fonts, art)
	$(DKA) make -C app APP_VERSION=$(VERSION)

# The splash is signed with the HOME Menu's key (tools/logo.py), read from the console's own
# HOME Menu code; first build with makerom's homebrew logo, which it reworks.
HOME_MENU_CODE ?= ../3ds-ua/work/0004003000009802/code.bin

cia: app ## the app as a CIA with its HOME Menu banner and splash -> build/3ds-ua-hub-$(VERSION).cia
	$(BANNERTOOL) makebanner -i build/art/banner.png -a build/art/banner.wav -o build/art/banner.bnr
	$(MAKE) makerom
	@if [ -f $(HOME_MENU_CODE) ]; then \
		$(PY) tools/logo.py render $(CIA) build/fonts/Nunito-800.ttf build/logo && \
		$(DKA) sh -c 'for n in waves logo; do /opt/devkitpro/tools/bin/tex3ds -f etc1 -q high -z none -r -o build/logo/$$n.etc1 build/logo/$$n.png || exit 1; done' && \
		$(PY) tools/logo.py pack build/logo $(HOME_MENU_CODE) && \
		$(MAKE) makerom CIA_LOGO="-logo ../build/logo/logo.bcma.lz"; \
	else echo "no $(HOME_MENU_CODE): the CIA keeps makerom's homebrew splash"; fi
	@ls -la $(CIA)

makerom:
	cd app && ../$(MAKEROM) -f cia -o ../$(CIA) -target t -exefslogo $(CIA_LOGO) -rsf cia.rsf \
		-elf 3ds-ua-hub.elf -icon 3ds-ua-hub.smdh -banner ../build/art/banner.bnr \
		-major $(word 1,$(subst ., ,$(VERSION))) -minor $(word 2,$(subst ., ,$(VERSION))) -micro $(word 3,$(subst ., ,$(VERSION)))
	$(PY) tools/cia_version.py $(CIA) $(VERSION)

test: ## host tests of the app's portable code
	@mkdir -p build
	cc -std=c99 -O2 -Wall -Wextra -Werror -o build/nav_test app/tests/nav_test.c app/source/nav.c
	build/nav_test
	cc -std=c99 -O2 -Wall -Wextra -Werror -o build/catalog_test app/tests/catalog_test.c app/source/catalog.c \
		app/vendor/cJSON.c app/vendor/monocypher.c app/vendor/monocypher-ed25519.c
	build/catalog_test
	cc -std=c99 -O2 -Wall -Wextra -Werror -D_DEFAULT_SOURCE -o build/view_test app/tests/view_test.c app/source/view.c
	build/view_test
	cc -std=c99 -O2 -Wall -Wextra -Werror -D_DEFAULT_SOURCE -o build/fsutil_test app/tests/fsutil_test.c \
		app/source/core/fsutil.c app/source/core/sha256.c
	build/fsutil_test build/fsutil_sd
	cc -std=c99 -O2 -Wall -Wextra -Werror -o build/resolve_test app/tests/resolve_test.c app/source/resolve.c app/source/catalog.c \
		app/vendor/cJSON.c app/vendor/monocypher.c app/vendor/monocypher-ed25519.c
	build/resolve_test
	cc -std=c99 -O2 -Wall -Wextra -Werror -D_DEFAULT_SOURCE -o build/patch_test app/tests/patch_test.c app/source/core/patch.c \
		app/source/core/record.c app/source/core/unzip.c app/source/core/fsutil.c app/source/core/sha256.c -lz
	build/patch_test build/patch_sd
	cc -std=c99 -O2 -Wall -Wextra -Werror -o build/errors_test app/tests/errors_test.c app/source/errors.c
	build/errors_test
	cc -std=c99 -O2 -Wall -Wextra -Werror -o build/report_test app/tests/report_test.c app/source/core/report.c
	build/report_test
	cc -std=c99 -O2 -Wall -Wextra -Werror -o build/qr_test app/tests/qr_test.c app/vendor/qrcodegen.c
	build/qr_test

clean: ## remove what the targets above make
	$(DKA) make -C app clean
	rm -rf build app/romfs
