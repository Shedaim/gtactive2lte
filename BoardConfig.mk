DEVICE_PATH := device/samsung/gtactive2lte

# inherit from the proprietary version
-include vendor/samsung/gtactive2lte/BoardConfigVendor.mk

################ General #######################
TARGET_SOC := exynos7870
TARGET_BOARD_PLATFORM := exynos5
BOARD_VENDOR := samsung
TARGET_POWERHAL_VARIANT := samsung
TARGET_BUILD_VARIANT := eng
TARGET_BOOTLOADER_BOARD_NAME := universal7880
PLATFORM_VERSION=7.1.1


################# arm Architecture #####################
TARGET_ARCH := arm
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_VARIANT := cortex-a53
TARGET_CPU_CORTEX_A53 := true


############### Board Configurations #################
BOARD_KERNEL_CMDLINE := 
BOARD_PAGE_SIZE := 2048
BOARD_KERNEL_BASE := 0x10000000
BOARD_KERNEL_PAGESIZE := 2048
BOARD_HASH_TYPE := sha1
BOARD_OS_VERSION := 7.1.1
BOARD_OS_PATCH_LEVEL := 2017-10
BOARD_DT_SIZE := 501760

BOARD_MKBOOTIMG_ARGS := --kernel_offset 0x00008000 --ramdisk_offset 0x01000000 --second_offset 0x00f00000 --tags_offset 0x00000100 --board SRPQE17A000RU --dt $(DEVICE_PATH)/dt.img

# fix this up by examining /proc/mtd on a running device
BOARD_BOOTIMAGE_PARTITION_SIZE := 33554432
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 39845888
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 3221225472
BOARD_USERDATAIMAGE_PARTITION_SIZE := 11936989184
#BOARD_USERDATAIMAGE_PARTITION_SIZE := 3221225472
BOARD_FLASH_BLOCK_SIZE := 131072

# Use this flag if the board has a ext4 partition larger than 2gb
BOARD_HAS_LARGE_FILESYSTEM := true
TARGET_USERIMAGES_USE_EXT4 := true
TARGET_USERIMAGES_USE_F2FS := true
BOARD_USES_HWC_SERVICES := true
BOARD_SEPOLICY_DIRS := $(DEVICE_PATH)/sepolicy


################# Kernel Sources ########################
TARGET_USES_UNCOMPRESSED_KERNEL := true
TARGET_PREBUILT_KERNEL := false
TARGET_KERNEL_BUILT_FROM_SOURCE := true
TARGET_KERNEL_SOURCE := $(DEVICE_PATH)/kernel
BOARD_HAS_NO_SELECT_BUTTON := true
TARGET_KERNEL_CONFIG := exynos7870-gtactive2lte_eur_open_defconfig
TARGET_SYSTEM_PROP += $(DEVICE_PATH)/system.prop
TARGET_KERNEL_ARCH := arm64
TARGET_KERNEL_HEADER_ARCH := arm64


################## Recovery ########################
#TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/ramdisk/fstab.samsungexynos7870
TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/recovery.fstab

############# TWRP specific build flags ##################
TW_THEME := portrait_mdpi
<<<<<<< HEAD

# Download mode for Samsung devices
TW_HAS_DOWNLOAD_MODE := true

# remove button to reboot to bootloader as Samsung devices use Download mode
TW_NO_REBOOT_BOOTLOADER := true

=======
# RECOVERY_SDCARD_ON_DATA := true
# Download mode for Samsung devices
TW_HAS_DOWNLOAD_MODE := true
# remove button to reboot to bootloader as Samsung devices use Download mode
TW_NO_REBOOT_BOOTLOADER := true


>>>>>>> d666b1d72aecf6ffffd81b93d366adf57e7c16df
# No MTP in recovery. Allows easy use of adb automatically
TW_EXCLUDE_MTP := true

TW_INCLUDE_NTFS_3G := false

# exFAT drivers included in the kernel
TW_NO_EXFAT_FUSE := false

# No love for the wicked (device ships with M)
TW_EXTRA_LANGUAGES := true

# include Logcat daemon for help in debugging
TWRP_INCLUDE_LOGCAT := true

# Use Busybox
TW_USE_TOOLBOX := false

# Fix screen is flipped
BOARD_HAS_FLIPPED_SCREEN := true

# Encryption support
TW_INCLUDE_CRYPTO := true
TW_CRYPTO_FS_TYPE := "ext4"
