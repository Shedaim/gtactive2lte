USE_CAMERA_STUB := true

# inherit from the proprietary version
-include vendor/samsung/gtactive2lte/BoardConfigVendor.mk

TARGET_ARCH := arm
TARGET_NO_BOOTLOADER := true
TARGET_BOARD_PLATFORM := universal7870
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_VARIANT := cortex-a7
TARGET_CPU_SMP := true
ARCH_ARM_HAVE_TLS_REGISTER := true
TARGET_BOOTLOADER_BOARD_NAME := gtactive2lte
TARGET_BUILD_VARIANT=eng

############### Board Configurations #################

BOARD_KERNEL_CMDLINE := 
BOARD_KERNEL_BASE := 10000000
BOARD_KERNEL_PAGESIZE := 2048
BOARD_HASH_TYPE := sha1
BOARD_KERNEL_OFFSET := 00008000
BOARD_RAMDISK_OFFSET := 01000000
BOARD_SECOND_OFFSET := 00f00000
BOARD_TAGS_OFFSET := 00000100
BOARD_OS_VERSION := 7.1.1
BOARD_OS_PATCH_LEVEL := 2017-10
BOARD_DT_SIZE := 501760

# fix this up by examining /proc/mtd on a running device
BOARD_BOOTIMAGE_PARTITION_SIZE := 33554432
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 39845888
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 3137544192
BOARD_USERDATAIMAGE_PARTITION_SIZE := 11708616704
BOARD_FLASH_BLOCK_SIZE := 131072


################# Kernel Sources ########################

#TARGET_PREBUILT_KERNEL := device/samsung/gtactive2lte/kernel
TARGET_KERNEL_BUILT_FROM_SOURCE := true
TARGET_KERNEL_SOURCE := device/samsung/gtactive2lte/Kernel
BOARD_HAS_NO_SELECT_BUTTON := true


############# TWRP specific build flags ##################

TARGET_SCREEN_WIDTH = 720
TARGET_SCREEN_HEIGHT = 1080
RECOVERY_SDCARD_ON_DATA := true
TARGET_RECOVERY_PIXEL_FORMAT := "ABGR_8888"
TARGET_USE_CUSTOM_LUN_FILE_PATH := "/sys/devices/15400000.usb/15400000.dwc3/gadget/lun%d/file"
TW_BRIGHTNESS_PATH := "/sys/devices/13900000.dsim/backlight/panel/brightness"
TW_MAX_BRIGHTNESS := 255
TW_DEFAULT_BRIGHTNESS := 162
TW_NO_REBOOT_BOOTLOADER := true

# Download mode for Samsung devices
TW_HAS_DOWNLOAD_MODE := true

TW_INCLUDE_NTFS_3G := false

# exFAT drivers included in the kernel
TW_NO_EXFAT_FUSE := false

# No love for the wicked (device ships with M)
TW_EXTRA_LANGUAGES := true

# add support for encryption
TW_INCLUDE_CRYPTO := true

# include Logcat daemon for help in debugging
TWRP_INCLUDE_LOGCAT := true

#Use Busybox
TW_USE_TOOLBOX := false

#For Samsung devices - Inject TWRP as second ramdisk.
TW_INCLUDE_INJECTTWRP := true
# If your device has recovery as a second ramdisk of boot.img
TW_HAS_NO_RECOVERY_PARTITION := true










