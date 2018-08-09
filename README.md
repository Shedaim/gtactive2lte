TWRP device tree for Samsung Galaxy Tab Active 2 (Exynos)

**************************************************************
************************Disclaimer****************************
***********All flashing is done at your own risk!*************
****While nothing from this repo should break your device,****
*******don't come back here blaming anyone if it does!********
**************************************************************
**************************************************************

General Info:
Android version: 7.1.2
Kernel sources are available at: https://github.com/Shedaim/kernel_samsung_gtactive2lte.git
Stable builds were created using the Omni-TWRP sources: https://github.com/minimal-manifest-twrp/platform_manifest_twrp_omni.git -b twrp-7.1
This repo will install TWRP-3.2.3
Successfull SuperSU installation was done with version v2.82-SR5 - link from Chainfire -  http://download.chainfire.eu/1220/SuperSU/SR5-SuperSU-v2.82-SR5-20171001224502.zip?retrieve_file=1

Building process:
Build points to $(Omni_Sources_Dircetory)/kernel/samsung/gtactive2lte <-- this is where you should insert the kernel sources

cd $(Omni_Sources_Dircetory)
. build/envsetup.sh
lunch omni_gtactive2lte-eng
make -j$(Number of cores * 2) recoveryimage

*(This will also build kernel out of configuration "exynos7870-gtactive2lte_eur_open_defconfig" in the kernel sources)


After Bulding:
Recovery.img will be in directory out/target/product/gtactive2lte/
If flashing with Odin, need to compress the file into a recovery.tar file.
This can be done using linux native commands: "tar -cf recovery.tar recovery.img" (Done from inside directory)


Installation process:
1. Flash Stock ROM.
2. Flash recovery using Odin (AP = recovery.tar), remember to untick the "auto-reboot" option.
3. Load recovery immediately after flashing (DO NOT let it boot to system).
4. Format data (Will erase /data partition) - ###THIS WILL DELETE ALL YOUR DATA, SO DO BACKUP BEFORE###
5. Reboot to recovery.
6. ADB push SuperSU to internal storage / insert SD-Card with SuperSU.zip.
7. Flash SuperSU using TWRP install option.
8. Reboot to system (May bootloop several times. Wait untill system loads).


Done. Enjoy!

#######################################################################################################
Many thanks to TWRP team for the support on building this repo/device_tree/recovery.
Special thanks to CaptainThrowback, Bigbiff and Dees_Troy on TWRP-freenode and Ashyx on xda-developers.
#######################################################################################################
