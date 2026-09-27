# ENet (UDP networking used by NetManager), built from the upstream sources
# linked at enet-src/ (see flare-android-project/setup_android_deps.sh).
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := enet
LOCAL_C_INCLUDES := $(LOCAL_PATH)/enet-src/include
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/enet-src/include
LOCAL_CFLAGS := -DHAS_SOCKLEN_T=1 -DHAS_POLL=1 -DHAS_FCNTL=1 -DHAS_GETADDRINFO=1 -DHAS_GETNAMEINFO=1 -DHAS_INET_PTON=1 -DHAS_INET_NTOP=1 -DHAS_MSGHDR_FLAGS=1
LOCAL_SRC_FILES := enet-src/callbacks.c enet-src/compress.c enet-src/host.c enet-src/list.c \
	enet-src/packet.c enet-src/peer.c enet-src/protocol.c enet-src/unix.c
include $(BUILD_STATIC_LIBRARY)
