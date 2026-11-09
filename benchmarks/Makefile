.DEFAULT_GOAL := all
.PHONY: all hpl package config plan run smoke doctor test clean help
all hpl package config plan run smoke doctor test clean help:
	+$(MAKE) -C hpl $(if $(filter hpl,$@),all,$@)
