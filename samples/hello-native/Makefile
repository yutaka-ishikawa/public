EAPP := hello-native
HOST := hello-native-runner
HOST_SOURCE := host/host_native.cpp
PACKAGE := hello-native.ke
include ../common.mk
all: $(BUILD_DIR)/$(PACKAGE)
$(BUILD_DIR)/$(EAPP): eapp/eapp_native.c $(EAPP_LIBS) Makefile ../common.mk | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) $(NATIVE_FLAGS) -MMD -MP -MF "$@.d" $(LDFLAGS) "$<" $(EAPP_LIBS) $(LDLIBS) -o "$@"
	python3 "$(REPO_DIR)/scripts/check-keystone-native-entry.py" "$(CROSS_COMPILE)nm" "$@"
$(BUILD_DIR)/$(PACKAGE): $(BUILD_DIR)/$(EAPP) $(BUILD_DIR)/$(HOST) $(RUNTIME_FILES) Makefile ../common.mk LICENSE
	mkdir -p "$(BUILD_DIR)/pkg"
	cp "$(BUILD_DIR)/$(EAPP)" "$(BUILD_DIR)/$(HOST)" $(RUNTIME_FILES) LICENSE "$(BUILD_DIR)/pkg/"
	$(MAKESELF) --noprogress "$(BUILD_DIR)/pkg" "$@" "Keystone hello-native sample" ./$(HOST) $(EAPP) eyrie-rt loader.bin 0
