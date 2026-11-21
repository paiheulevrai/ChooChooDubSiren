LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := main
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include $(LOCAL_PATH)/../SDL/include
LOCAL_SRC_FILES := main.cpp siren.cpp fonts16x24.cpp
LOCAL_CPPFLAGS := -std=c++17 -Wall -Wextra
LOCAL_SHARED_LIBRARIES := SDL2
LOCAL_LDLIBS := -llog -landroid
include $(BUILD_SHARED_LIBRARY)
