SHELL := /bin/bash

PIO ?= pio
BAUD ?= 115200
PORT ?=

ifdef PORT
PORT_ARGS := --upload-port $(PORT)
MONITOR_PORT_ARGS := --port $(PORT)
else
PORT_ARGS :=
MONITOR_PORT_ARGS :=
endif

.PHONY: help build upload monitor clean rebuild all

help:
	@echo "Targets:"
	@echo "  make build      - Build firmware"
	@echo "  make upload     - Build and upload firmware"
	@echo "  make monitor    - Open serial monitor"
	@echo "  make all        - Build, upload, then monitor"
	@echo "  make clean      - Clean build artifacts"
	@echo "  make rebuild    - Clean then build"
	@echo ""
	@echo "Optional vars:"
	@echo "  PORT=/dev/cu.usbserial-XXXX"
	@echo "  BAUD=115200"

build:
	$(PIO) run

upload:
	$(PIO) run --target upload $(PORT_ARGS)

monitor:
	$(PIO) device monitor $(MONITOR_PORT_ARGS) --baud $(BAUD)

all: build upload monitor

clean:
	$(PIO) run --target clean

rebuild: clean build
