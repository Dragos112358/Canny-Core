AUTHOR=Radu Daia <radu_petru.daia@upb.ro>
VERSION=0.1

APPTAINER=apptainer
BUILD_DIR=./build
FLAGS=--fakeroot --force
SIF_FILES=debug-tools.sif


# default example for running a container
DEBUG_TOOLS_SIF=debug-tools.sif
EXAMPLE_ARGS=$(BUILD_DIR)/$(DEBUG_TOOLS_SIF) perf stat ls
ARGS:=$(EXAMPLE_ARGS)

all: $(SIF_FILES)

run_example:
	$(APPTAINER) run $(EXAMPLE_ARGS)

run:
	$(APPTAINER) run $(ARGS)

%.sif: %.def
	mkdir -p $(BUILD_DIR)
	$(APPTAINER) build $(FLAGS) $(BUILD_DIR)/$@ $<

.PHONY: clean

clean: 
	rm -rf $(BUILD_DIR)
