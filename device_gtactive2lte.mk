$(call inherit-product, $(SRC_TARGET_DIR)/product/languages_full.mk)

ifeq ($(TARGET_PREBUILT_KERNEL),)
	LOCAL_KERNEL := device/samsung/gtactive2lte/
else
	LOCAL_KERNEL := $(TARGET_PREBUILT_KERNEL)
endif

PRODUCT_COPY_FILES += \
    $(LOCAL_KERNEL):kernel

# Ramdisk
PRODUCT_PACKAGES += \
    fstab.samsungexynos7870 \
    init.baseband.rc \
    init.samsungexynos7870.rc \
    init.samsungexynos7870.usb.rc \
    init.rilcommon.rc \
    init.wifi.rc \
    ueventd.samsungexynos7870.rc

$(call inherit-product, build/target/product/full.mk)

PRODUCT_BUILD_PROP_OVERRIDES += BUILD_UTC_DATE=0
PRODUCT_NAME := omni_gtactive2lte
PRODUCT_DEVICE := gtactive2lte

