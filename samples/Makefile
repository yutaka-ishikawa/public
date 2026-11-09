.DEFAULT_GOAL := all
SAMPLES := helloworld hello-native attestation tests
.PHONY: all check clean $(SAMPLES)
all: $(SAMPLES)
$(SAMPLES):
	$(MAKE) -C $@ all
check:
	@set -e; for sample in $(SAMPLES); do $(MAKE) -C $$sample check; done
clean:
	@set -e; for sample in $(SAMPLES); do $(MAKE) -C $$sample clean; done
