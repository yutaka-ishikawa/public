.DEFAULT_GOAL := test
.PHONY: all test clean
all:
	$(MAKE) -C sbi-2001 all
	$(MAKE) -C sbi-2002 all
	$(MAKE) -C sbi-2003 all
	$(MAKE) -C sbi-2004 all
	$(MAKE) -C sbi-2005 all
	$(MAKE) -C edgecall all
test:
	bash run-all.sh
clean:
	$(MAKE) -C sbi-2001 clean
	$(MAKE) -C sbi-2002 clean
	$(MAKE) -C sbi-2003 clean
	$(MAKE) -C sbi-2004 clean
	$(MAKE) -C sbi-2005 clean
	$(MAKE) -C edgecall clean
