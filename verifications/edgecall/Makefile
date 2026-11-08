.DEFAULT_GOAL := guest
.PHONY: all guest test clean kernel-module kernel-object
all guest:
	$(MAKE) -f guest/guest.mk guest
test:
	bash run-guest.sh
kernel-module:
	$(MAKE) -f guest/guest.mk kernel-module
kernel-object:
	$(MAKE) -f guest/guest.mk kernel-object
clean:
	rm -rf build
