# really just some handy scripts...

# use BrcmPatchRAM.kext on 10.10 and prior
# use BrcmPatchRAM2.kext on 10.11 and later
VERSION_ERA=$(shell ./print_version.sh)
ifeq "$(VERSION_ERA)" "10.10-"
KEXT=BrcmPatchRAM.kext
INSTDIR=/System/Library/Extensions
else
KEXT=BrcmPatchRAM2.kext
INSTDIR=/Library/Extensions
endif

# Note: BrcmFirmwareStore.kext obsolete (but still deleted here if present)

# for using BrcmFirmwareRepo.kext (firmware in Resources)
KEXT2=BrcmFirmwareRepo.kext
KEXTDEL1=BrcmFirmwareData.kext
KEXTDEL2=BrcmFirmwareStore.kext

# for using BrcmFirmwareData.kext (firmware in the kext itself)
#KEXT2=BrcmFirmwareData.kext
#KEXTDEL1=BrcmFirmwareRepo.kext
#KEXTDEL2=BrcmFirmwareStore.kext

INJECT=BrcmBluetoothInjector.kext
DIST=RehabMan-BrcmPatchRAM
#INSTDIR=/kexts
BUILDDIR=./Build/Products

ifeq ($(findstring 32,$(BITS)),32)
OPTIONS:=$(OPTIONS) -arch i386
endif

ifeq ($(findstring 64,$(BITS)),64)
OPTIONS:=$(OPTIONS) -arch x86_64
endif

OPTIONS:=$(OPTIONS)

.PHONY: all
all:
	xcodebuild build $(OPTIONS) -scheme "BrcmPatchRAM" -configuration Debug
	xcodebuild build $(OPTIONS) -scheme "BrcmPatchRAM" -configuration Release

.PHONY: clean
clean:
	xcodebuild clean $(OPTIONS) -scheme "BrcmPatchRAM" -configuration Debug
	xcodebuild clean $(OPTIONS) -scheme "BrcmPatchRAM" -configuration Release

.PHONY: update_kernelcache
update_kernelcache:
	if [ "$(INSTDIR)" != "/kexts" ]; then sudo touch $(INSTDIR) && sudo kextcache -update-volume /; fi

.PHONY: install_debug
install_debug:
	if [ "$(KEXTDEL1)" != "" ]; then sudo rm -Rf $(INSTDIR)/$(KEXTDEL1); fi
	if [ "$(KEXTDEL2)" != "" ]; then sudo rm -Rf $(INSTDIR)/$(KEXTDEL2); fi
	sudo cp -R $(BUILDDIR)/Debug/$(KEXT) $(INSTDIR)
	if [ "`which tag`" != "" ]; then sudo tag -a Purple $(INSTDIR)/$(KEXT); fi
	sudo cp -R $(BUILDDIR)/Debug/$(KEXT2) $(INSTDIR)
	if [ "`which tag`" != "" ]; then sudo tag -a Purple $(INSTDIR)/$(KEXT2); fi
	make update_kernelcache

.PHONY: install
install:
	if [ "$(KEXTDEL1)" != "" ]; then sudo rm -Rf $(INSTDIR)/$(KEXTDEL1); fi
	if [ "$(KEXTDEL2)" != "" ]; then sudo rm -Rf $(INSTDIR)/$(KEXTDEL2); fi
	sudo cp -R $(BUILDDIR)/Release/$(KEXT) $(INSTDIR)
	if [ "`which tag`" != "" ]; then sudo tag -a Blue $(INSTDIR)/$(KEXT); fi
	sudo cp -R $(BUILDDIR)/Release/$(KEXT2) $(INSTDIR)
	if [ "`which tag`" != "" ]; then sudo tag -a Blue $(INSTDIR)/$(KEXT2); fi
	make update_kernelcache

.PHONY: install_inject
install_inject:
	sudo cp -R $(BUILDDIR)/Release/$(INJECT) $(INSTDIR)
	if [ "`which tag`" != "" ]; then sudo tag -a Blue $(INSTDIR)/$(INJECT); fi
	make update_kernelcache

.PHONY: load
load:
	sudo kextload $(INSTDIR)/$(KEXT)

.PHONY: unload
unload:
	sudo kextunload -p $(INSTDIR)/$(KEXT)

.PHONY: distribute
distribute:
	if [ -e ./Distribute ]; then rm -r ./Distribute; fi
	mkdir ./Distribute
	#cp -R $(BUILDDIR)/Debug ./Distribute
	cp -R $(BUILDDIR)/Release ./Distribute
	rm -Rf ./Distribute/Release/BrcmBluetoothInjector.kext
	find ./Distribute -path *.DS_Store -delete
	find ./Distribute -path *.dSYM -exec echo rm -r {} \; >/tmp/org.voodoo.rm.dsym.sh
	chmod +x /tmp/org.voodoo.rm.dsym.sh
	/tmp/org.voodoo.rm.dsym.sh
	rm /tmp/org.voodoo.rm.dsym.sh
	ditto -c -k --sequesterRsrc --zlibCompressionLevel 9 ./Distribute ./Archive.zip
	mv ./Archive.zip ./Distribute/`date +$(DIST)-%Y-%m%d.zip`

