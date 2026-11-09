.DEFAULT_GOAL := guest
.PHONY: all guest test kernel-modules kernel-objects clean
all guest:
	$(MAKE) -C edgecall guest OUT=build/guest FREE_MEM_MIB=2
	$(MAKE) -C edgecall guest OUT=build/guest-256 FREE_MEM_MIB=256
	$(MAKE) -C sbi-errors guest
	$(MAKE) -C random guest
	$(MAKE) -C crypto guest
	$(MAKE) -C sbi-3004 guest
	$(MAKE) -C sbi-4000 guest
	$(MAKE) -C robustness guest
test:
	bash run-all.sh
kernel-modules:
	$(MAKE) -C edgecall kernel-module
	$(MAKE) -C sbi-errors kernel-module
	$(MAKE) -C sbi-4000 kernel-module
	$(MAKE) -C robustness kernel-module
kernel-objects:
	$(MAKE) -C edgecall kernel-object
	$(MAKE) -C sbi-errors kernel-object
	$(MAKE) -C sbi-4000 kernel-object
	$(MAKE) -C robustness kernel-object
clean:
	$(MAKE) -C edgecall clean
	$(MAKE) -C sbi-errors clean
	$(MAKE) -C random clean
	$(MAKE) -C crypto clean
	$(MAKE) -C sbi-3004 clean
	$(MAKE) -C sbi-4000 clean
	$(MAKE) -C robustness clean

.PHONY: robustness-test
robustness-test:
	$(MAKE) -C robustness test TEST_ARGS="$(TEST_ARGS)"
