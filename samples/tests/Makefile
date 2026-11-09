HOST := test-runner
HOST_SOURCE := test-runner.cpp
PACKAGE := tests.ke
include ../common.mk
TESTS := stack loop malloc long-nop fibonacci fib-bench attestation untrusted data-sealing
APPS := $(addprefix $(BUILD_DIR)/test-,$(TESTS))
all: $(BUILD_DIR)/$(PACKAGE)
$(BUILD_DIR)/%.o: %.c Makefile ../common.mk
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) -fno-stack-protector -fno-builtin -MMD -MP -MF "$@.d" -c "$<" -o "$@"
define native_c
$(BUILD_DIR)/test-$(1): $(BUILD_DIR)/$(1)/$(1).o $(2) $(BUILD_DIR)/heap.o $(EAPP_LIBS) Makefile ../common.mk
	$(CC) $(CPPFLAGS) $(CFLAGS) $(NATIVE_FLAGS) $(LDFLAGS) $(BUILD_DIR)/$(1)/$(1).o $(2) $(BUILD_DIR)/heap.o $(EAPP_LIBS) $(LDLIBS) -o "$$@"
	python3 "$(REPO_DIR)/scripts/check-keystone-native-entry.py" "$(CROSS_COMPILE)nm" "$$@"
endef
$(foreach test,malloc fibonacci fib-bench attestation data-sealing,$(eval $(call native_c,$(test),)))
$(eval $(call native_c,untrusted,$(BUILD_DIR)/untrusted/edge_wrapper.o))
-include $(wildcard $(BUILD_DIR)/*/*.d)
$(BUILD_DIR)/test-stack: stack/stack.S $(EAPP_LIBS) Makefile ../common.mk | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) $(NATIVE_FLAGS) $(LDFLAGS) $< $(EAPP_LIBS) $(LDLIBS) -o "$@"
$(BUILD_DIR)/test-loop: loop/loop.s $(EAPP_LIBS) Makefile ../common.mk | $(BUILD_DIR)
	$(CC) -x assembler-with-cpp $(CPPFLAGS) $(INCLUDES) $(CFLAGS) $(NATIVE_FLAGS) $(LDFLAGS) $< -x none $(EAPP_LIBS) $(LDLIBS) -o "$@"
$(BUILD_DIR)/test-long-nop: long-nop/long-nop.S long-nop/nop.h $(EAPP_LIBS) Makefile ../common.mk | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) $(NATIVE_FLAGS) $(LDFLAGS) $< $(EAPP_LIBS) $(LDLIBS) -o "$@"
$(BUILD_DIR)/$(PACKAGE): $(APPS) $(BUILD_DIR)/$(HOST) run-tests.sh $(RUNTIME_FILES) Makefile ../common.mk LICENSE
	mkdir -p "$(BUILD_DIR)/pkg"
	cp $(APPS) "$(BUILD_DIR)/$(HOST)" run-tests.sh $(RUNTIME_FILES) LICENSE "$(BUILD_DIR)/pkg/"
	$(MAKESELF) --noprogress "$(BUILD_DIR)/pkg" "$@" 'Keystone upstream tests' bash ./run-tests.sh
