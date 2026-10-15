LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE     := SAKeyboard
LOCAL_SRC_FILES  := main.cpp
LOCAL_CPPFLAGS   += -std=c++17 -fexceptions -O2
# Put AML's "mod" headers folder (from the AndroidModLoader repo) in ./AML_headers
LOCAL_C_INCLUDES += $(LOCAL_PATH)/AML_headers
LOCAL_LDLIBS     := -llog
include $(BUILD_SHARED_LIBRARY)
