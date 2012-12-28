LOCAL_PATH := $(call my-dir)
#FFMPEG_DIR := ffmpeg-0.8.5/
FFMPEG_DIR := ffmpeg-master

LOGLEVEL                :=      trace
LOGLEVELS =
ifeq ($(LOGLEVEL),error)
        LOGLEVELS       += ERROR
endif
ifeq ($(LOGLEVEL),warn)
        LOGLEVELS       += ERROR WARN
endif
ifeq ($(LOGLEVEL),info)
        LOGLEVELS       += ERROR WARN INFO
endif
ifeq ($(LOGLEVEL),debug)
        LOGLEVELS       += ERROR WARN INFO DEBUG
endif
ifeq ($(LOGLEVEL),trace)
        LOGLEVELS       += ERROR WARN INFO DEBUG TRACE
endif


#ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
#   ARCH_DIR := armv7-a
#endif


ARCH_DIR := armv5te
LIBVERSION := vanilla
include $(CLEAR_VARS)
LOCAL_MODULE := ffmpeg$(LIBVERSION)-prebuilt
LOCAL_SRC_FILES := $(FFMPEG_DIR)/android/$(ARCH_DIR)/libffmpeg$(LIBVERSION).so
LOCAL_EXPORT_C_INCLUDES := $(FFMPEG_DIR)/android/$(ARCH_DIR)/include
LOCAL_EXPORT_LDLIBS := $(FFMPEG_DIR)/android/$(ARCH_DIR)/libffmpeg$(LIBVERSION).so
LOCAL_PRELINK_MODULE := true
include $(PREBUILT_SHARED_LIBRARY)

ARCH_DIR := armv6_vfp
LIBVERSION := vfp
include $(CLEAR_VARS)
LOCAL_MODULE := ffmpeg$(LIBVERSION)-prebuilt
LOCAL_SRC_FILES := $(FFMPEG_DIR)/android/$(ARCH_DIR)/libffmpeg$(LIBVERSION).so
LOCAL_EXPORT_C_INCLUDES := $(FFMPEG_DIR)/android/$(ARCH_DIR)/include
LOCAL_EXPORT_LDLIBS := $(FFMPEG_DIR)/android/$(ARCH_DIR)/libffmpeg$(LIBVERSION).so
LOCAL_PRELINK_MODULE := true
include $(PREBUILT_SHARED_LIBRARY)

ARCH_DIR := armv7-a
LIBVERSION := neon
include $(CLEAR_VARS)
LOCAL_MODULE := ffmpeg$(LIBVERSION)-prebuilt
LOCAL_SRC_FILES := $(FFMPEG_DIR)/android/$(ARCH_DIR)/libffmpeg$(LIBVERSION).so
LOCAL_EXPORT_C_INCLUDES := $(FFMPEG_DIR)/android/$(ARCH_DIR)/include
LOCAL_EXPORT_LDLIBS := $(FFMPEG_DIR)/android/$(ARCH_DIR)/libffmpeg$(LIBVERSION).so
LOCAL_PRELINK_MODULE := true
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_ALLOW_UNDEFINED_SYMBOLS=false
LOCAL_MODULE := ffinterface
LOCAL_SRC_FILES := ffmpegplayer.c
LOCAL_C_INCLUDES := $(LOCAL_PATH)/$(FFMPEG_DIR)
LOCAL_SHARED_LIBRARY := ffmpeg$(LIBVERSION)-prebuilt
LOCAL_LDLIBS    := -llog -lz -lm $(LOCAL_PATH)/$(FFMPEG_DIR)/android/$(ARCH_DIR)/libffmpeg$(LIBVERSION).so
LOCAL_CFLAGS := -g $(foreach ll,$(LOGLEVELS),-DAACD_LOGLEVEL_$(ll))

include $(BUILD_SHARED_LIBRARY)
