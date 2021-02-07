BrcmPatchRAM
============

[![Build Status](https://github.com/acidanthera/BrcmPatchRAM/workflows/CI/badge.svg?branch=master)](https://github.com/acidanthera/BrcmPatchRAM/actions) [![Scan Status](https://scan.coverity.com/projects/22191/badge.svg?flat=1)](https://scan.coverity.com/projects/22191)

#### 翻译语言

- 简体中文
- [English](./README.md)

大多数Broadcom USB蓝牙设备都使用称为RAMUSB的系统。 RAMUSB允许动态更新设备的固件，但是在关闭计算机时，先前应用的任何更新都会丢失。

Broadcom Windows驱动程序将在每次启动时将固件上载到Broadcom蓝牙设备中，但是，对于macOS，此功能不可用。 `BrcmPatchRAM` kext是一个macOS驱动程序，适用于基于Broadcom RAMUSB的设备的PatchRAM更新。 每次启动/唤醒时，它将固件更新应用于Broadcom蓝牙设备，与Windows驱动程序相同。 应用的固件是从Windows驱动程序中提取的，并且功能应与Windows相同。

请注意，原始的Apple Broadcom蓝牙设备不是RAMUSB设备，因此没有相同的固件机制。

### 安装

__请注意，如果您有Apple MacBook / iMac / Mac Pro等，请按照 [Mac instructions](https://github.com/acidanthera/BrcmPatchRAM/blob/master/README-Mac.md)__

根据macOS版本安装`BrcmPatchRAM.kext`或`BrcmPatchRAM2.kext`或`BrcmPatchRAM3.kext`其中之一，决不要三者都安装。

  * `BrcmPatchRAM.kext`: 适用于 10.10 或更早版本.

  * `BrcmPatchRAM2.kext`: 适用于 10.11-10.14.
  
* `BrcmPatchRAM3.kext`: 适用于 10.15 或更高版本

另外，根据安装位置安装一个固件kext BrcmFirmwareData.kext或BrcmFirmwareRepo.kext，决不要同时安装。

  * `BrcmFirmwareData.kext`: 最适合引导加载程序注入。 这是首选配置。

  * `BrcmFirmwareRepo.kext`: 安装到`/System/Library/Extensions`（在10.11及更高版本上为`/Library/Extensions`）。 该kext的内存效率比`BrcmFirmwareData.kext`略高，但是不能由引导加载程序注入。

  * 高级用户：对于自定义固件注入器，请安装注入器以及`BrcmFirmwareRepo.kext`。 这可以从`/System/Library/Extensions`或通过引导加载程序注入工作。 （可选）您可以从`BrcmFirmwareRepo.kext/Contents/Resources`中删除所有固件。 如果通过引导加载程序使用注入器，则必须将`BrcmFirmwareRepo.kext`的`Info.plist`中的`IOProviderClass`从`disabled_IOResources`更改为`IOResources`。

另外，如果您有非PatchRAM设备（或者不确定），请安装macOS版本的`BrcmNonPatchRAM.kext`或`BrcmNonPatchRAM2.kext`之一，请不要同时安装两者。尽管这些kext不安装任何固件（这些设备内置固件），但它们仍依赖`BrcmPatchRAM.kext` / `BrcmPatchRAM2.kext`。

  * `BrcmNonPatchRAM.kext`: 适用于 10.10 或更早版本.

  * `BrcmNonPatchRAM2.kext`: 适用于 10.11 或更高版本.

### BrcmBluetoothInjector.kext

用于macOS 10.11或更高版本，对于较旧的系统，请使用`BrcmBluetoothInjectorLetacy.kext`；使用`BrcmPatchRAM3.kext`还需要`BrcmBluetoothInjector.kext`，因为macOS Catalina（10.15）中的更改要求使用单独的注射器注入到kext。 这是由于删除了以下IOCatalogue方法：

```bash
IOCatalogue::addDrivers, IOCatalogue::removeDrivers and IOCatalogue::startMatching
```

因此，为了使设备（`BroadcomBluetoothHostControllerUSBTransport`）加载本机BT驱动程序，我们使用IOProbeScore稍低于BrcmPatchRAM3的plist进行注入，因此它不会在固件上传之前进行探测。

`BrcmBluetoothInjector.kext`是[无代码内核扩展](https://developer.apple.com/library/archive/documentation/Darwin/Conceptual/KEXTConcept/KEXTConceptAnatomy/kext_anatomy.html)，它使用plist注入BT硬件数据； 它不包含固件上载器。 如果希望查看您的设备在没有固件上传器的情况下是否可以运行，则可能还需要尝试此kext。

请勿在此kext上使用`BrcmPatchRAM`或`BrcmPatchRAM2`。

`BrcmBluetoothInjector` supported devices:

  * ``[0489:e032]`` 20702 E032 Combo
  * ``[0489:e042]`` 20702A1 Lenovo China standalone
  * ``[0489:e046]`` 20702A1 Acer 43228+20702 combo card
  * ``[0489:e04f]`` 20702A1 Lenovo China 43227 WLAN + 20702A1 Combo card
  * ``[0489:e052]`` 20702 non-UHE Generic
  * ``[0489:e055]`` 43142A0 Acer combo
  * ``[0489:e059]`` Acer 43228 + 20702A1 combo
  * ``[0489:e079]`` Lenovo China 43162 NGFF
  * ``[0489:e07a]`` Lenovo China 4352+20702 NGFF
  * ``[0489:e087]`` Acer 43228 NGFF combo module
  * ``[0489:e096]`` BCM43142A0
  * ``[0489:e097]`` Acer Foxconn BCM4356A2 NGFF
  * ``[0489:e0a1]`` 20703A1 Lenovo 43602 NGFF combo
  * ``[04ca:2003]`` 20702A1 Lenovo China standalone
  * ``[04ca:2004]`` LiteOn 43228+20702 combo
  * ``[04ca:2005]`` LiteOn 43228+20702 combo
  * ``[04ca:2006]`` LiteOn 43142 combo
  * ``[04ca:2009]`` LiteOn 43142 combo
  * ``[04ca:200a]`` LiteOn 4352 combo
  * ``[04ca:200b]`` LiteOn 4352 combo
  * ``[04ca:200c]`` LiteOn 4352 combo
  * ``[04ca:200e]`` Liteon 43228 NGFF combo
  * ``[04ca:200f]`` Acer_LiteOn BCM20702A1_4352
  * ``[04ca:2012]`` Acer BCM943142Y NGFF
  * ``[04ca:2013]`` Acer LiteOn BCM4356A2 NGFF
  * ``[04ca:2014]`` Asus LiteOn BCM4356A2 NGFF
  * ``[04ca:2016]`` Lenovo 43162 NGFF combo module
  * ``[04f2:b4a1]`` ASUS Chicony BCM43142A0 NGFF
  * ``[04f2:b4a2]`` BCM4356A2
  * ``[050d:065a]`` 20702 standalone
  * ``[0930:021e]`` 20702A1 Toshiba standalone
  * ``[0930:021f]`` Toshiba 43142
  * ``[0930:0221]`` 20702A1 Toshiba 4352
  * ``[0930:0223]`` 20702A1 Toshiba 4352
  * ``[0930:0225]`` Toshiba 43142 combo NGFF
  * ``[0930:0226]`` Toshiba 43142 combo NGFF
  * ``[0930:0229]`` 43162 combo NGFF
  * ``[0a5c:2168]`` BRCM Generic 43162Z
  * ``[0a5c:2169]`` BRCM Generic 43228z
  * ``[0a5c:216a]`` Dell DW1708 43142Y combo
  * ``[0a5c:216b]`` HP Rapture 4352z ngff combo
  * ``[0a5c:216c]`` HP Harrier 43142
  * ``[0a5c:216d]`` HP Hornet 43142Y ngff combo
  * ``[0a5c:216e]`` HP Blackbird 43162 NGFF
  * ``[0a5c:216f]`` Dell DW1560 4352+20702 M.2
  * ``[0a5c:217d]`` BCM2070 - BCM943224HMB, BCM943225HMB Combo
  * ``[0a5c:21d7]`` BRCM Generic 43142A0 RAMUSB
  * ``[0a5c:21de]`` 4352+20702A1 combo
  * ``[0a5c:21e1]`` 20702A1 non-UHE HP SoftSailing
  * ``[0a5c:21e3]`` 20702A1 non-UHE 4313 combo HP Valentine
  * ``[0a5c:21e6]`` 20702 non-UHE Lenovo Japan
  * ``[0a5c:21e8]`` 20702A1 dongles
  * ``[0a5c:21ec]`` 20702A1 REF6 OTP module standalone
  * ``[0a5c:21f1]`` 43228 combo
  * ``[0a5c:21f3]`` Lenovo Edge 43228 + 20702A1 combo
  * ``[0a5c:21f4]`` Lenovo Edge 4313 + 20702A1 combo
  * ``[0a5c:21fb]`` HP Supra 4352 20702A1 combo
  * ``[0a5c:21fd]`` BRCM Generic 4352z RAMUSB
  * ``[0a5c:640a]`` BRCM Generic Reference 4356
  * ``[0a5c:640b]`` HP Luffy 43228 + 20702 M.2
  * ``[0a5c:640e]`` Lenovo 4356 NGFF combo
  * ``[0a5c:6410]`` 20703A1 RAM download - DW1830 43602
  * ``[0a5c:6412]`` Dell 4350C5
  * ``[0a5c:6413]`` Broadcom Generic 4350C5
  * ``[0a5c:6414]`` Lenovo 4350C5
  * ``[0a5c:6417]`` Zebra 4352
  * ``[0a5c:6418]`` HP Brook 2x2ac
  * ``[0a5c:7460]`` 20703A1 RAM download
  * ``[0b05:17b5]`` Asus 43228+20702A1 combo
  * ``[0b05:17cb]`` 20702 standalone
  * ``[0b05:17cf]`` Asus 4352_20702A1 combo
  * ``[0b05:180a]`` Azurewave 4360+20702 combo
  * ``[0b05:181d]`` Asus AZUREWAVE MB BCM4356A2
  * ``[0bb4:0306]`` 20703A1 HTC runtime RAM dongle
  * ``[105b:e065]`` LenovoChina 43142A0 combo
  * ``[105b:e066]`` LenovoChina 43228+20702 combo
  * ``[13d3:3384]`` 20702A1 Azurewave standalone
  * ``[13d3:3388]`` BRCM Generic 43142A0 RAMUSB
  * ``[13d3:3389]`` BRCM Generic 43142A0 RAMUSB
  * ``[13d3:3392]`` Azurewave 43228+20702
  * ``[13d3:3404]`` 4352HMB Azurewave Module
  * ``[13d3:3411]`` Dell Alienware 4352 20702A1 combo
  * ``[13d3:3413]`` Azurewave 4360+20702 combo
  * ``[13d3:3418]`` Azurewave 4352+20702 combo module
  * ``[13d3:3427]`` Toshiba 43142 combo NGFF
  * ``[13d3:3435]`` AZUREWAVE BCM20702A1_4352
  * ``[13d3:3456]`` AZUREWAVE BCM20702A1_4352
  * ``[13d3:3473]`` Asus AZUREWAVE BCM4356A2 NGFF
  * ``[13d3:3482]`` AZUREWAVE BCM43142A0 NGFF
  * ``[13d3:3484]`` Acer AZUREWAVE BCM43142A0 NGFF
  * ``[13d3:3485]`` Asus AZUREWAVE BCM4356A2 NB 2217NF
  * ``[13d3:3488]`` Asus AZUREWAVE BCM4356A2 NB 2210
  * ``[13d3:3492]`` Asus AZUREWAVE BCM4356A2 NGFF
  * ``[13d3:3504]`` AW CM217NF BCM4371C2
  * ``[13d3:3508]`` AW ASUS CM217NF BCM4371C2
  * ``[13d3:3517]`` AW CE160H BCM20702
  * ``[145f:01a3]`` 20702A1 Asus Trust standalone
  * ``[2b54:5600]`` Emdoor AP6356SD BCM4356A2
  * ``[2b54:5601]`` Asus AP6356SDP1A BCM4356A2
  * ``[2b54:5602]`` AMPAK AP6356SDP2A BCM4356A2
  * ``[33ba:03e8]`` TOULINEUA BCM94360Z4 4360+20702 combo
  * ``[413c:8143]`` DW1550 4352+20702 combo
  * ``[413c:8197]`` Dell DW380 Nancy Blakes standalone

### 支持的设备

`BrcmPatchRAM`支持任何基于BCM20702芯片组的Broadcom USB蓝牙设备（可能也支持其他芯片组，但是尚未经过测试）。

目前支持以下设备：

* 标有 ***** 的设备已成功测试

非PatchRAM设备（BrcmPatchRAM用于加速睡眠后的恢复）：

  * ``[03f0:231d]`` HP 231d (ProBook BT built-in firmware)
  * ``[13d3:3295]`` Azurewave BCM943225 (20702A bult-in firmware)

经过测试的PatchRAM设备：
  * ``[0489:e032]`` 20702 Combo USB
  * ``[0489:e042]`` 20702A1 Lenovo China *
  * ``[0489:e079]`` Lenovo China 43162 NGFF
  * ``[0489:e07a]`` Lenovo NGFF (4352 / 20702)
  * ``[04ca:2003]`` 20702A1 Lenovo China
  * ``[04ca:200a]`` LiteOn (4352 Combo)
  * ``[04ca:200b]`` LiteOn (4352 Combo) *
  * ``[04ca:200c]`` LiteOn (4352 Combo)
  * ``[04ca:200f]`` Acer / LiteOn (4352 Combo) 
  * ``[050d:065a]`` Belkin (20702)
  * ``[0930:0221]`` Toshiba (4352 / 20702)
  * ``[0930:0223]`` Toshiba NGFF (4352 / 20702) *
  * ``[0a5c:216b]`` HP Rapture 4352Z NGFF Combo
  * ``[0a5c:216e]`` HP Blackbird 43162 NGFF
  * ``[0a5c:216f]`` Dell DW1560 (4352/20702)
  * ``[0a5c:21de]`` 4352/20702A1 combo
  * ``[0a5c:21e1]`` HP Softsailing (20702A1)
  * ``[0a5c:21e6]`` non-UHE Lenovo Bluetooth (20702)
  * ``[0a5c:21e8]`` Bluetooth USB Dongle (20702A1) *
  * ``[0a5c:21ec]`` Inateck Bluetooth (20702A1)
  * ``[0a5c:21fb]`` HP Supra 4352 (20702A1 Combo)
  * ``[0a5c:21fd]`` Broadcom 4352Z
  * ``[0a5c:22be]`` Broadcom BCM20702 Bluetooth 4.0 USB Device
  * ``[0a5c:6410]`` Dell Wireless 1830 Bluetooth 4.1 LE
  * ``[0a5c:6412]`` Dell Wireless 1820 Bluetooth 4.1 LE
  * ``[0b05:17cb]`` Asus BT-400 (20702 stand-alone) *
  * ``[0b05:17cf]`` Asus (4352/20702A1 combo) *
  * ``[0b05:180a]`` Azurewave (4360/20702 combo)
  * ``[13d3:3404]`` Azurewave (4352HMB) *
  * ``[13d3:3411]`` Dell Alienware (4352/20702A1 combo) *
  * ``[13d3:3413]`` Azurewave (4360/20702 combo)
  * ``[13d3:3418]`` Azurewave (4352/20702 combo)
  * ``[13d3:3435]`` Azurewave (4352/20702 combo)
  * ``[13d3:3456]`` Azurewave (4352/20702 combo)
  * ``[413c:8143]`` Dell DW1550 (4352/20702 combo)

Windows软件包中的所有固件都存在于kext中，并自动与其供应商/设备ID关联。 它们有望工作，但尚未得到确认。 如果您可以确认上面未列出的工作设备，请通过github上的issues数据库进行通知。固件已更新到版本12.0.1.1105。

### 更多安装细节

`BrcmPatchRAM.kext`或`BrcmPatchRAM2.kext`或`BrcmPatchRAM3.kext`可以通过引导加载程序kext注入安装，也可以放置在`/System/Library/Extensions`（在10.11及更高版本中为`/Library/Extensions`）中。
根据系统版本，仅安装一个，而不是安装三个。

`BrcmFirmwareRepo.kext`不适用于bootloader kext注入，除非使用特定于设备的固件注入器。
`BrcmFirmwareData.kext`可以与bootloader kext注入一起使用。

您还可以使用特定于设备的固件注入器（与`BrcmFirmwareRepo.kext`结合使用）。 在这种情况下，`BrcmFirmwareRepo.kext`确实可以从引导加载程序kexts中工作。

您可以在git存储库的`firmwares`目录中找到设备专用注射器。 它们不包含在发行版ZIP中。

### 配置

使用以下内核引导参数可以更改许多延迟。 如果发现在固件加载期间`BrcmPatchRAM`挂起，则可以更改这些值。 有关这些延迟的更多详细信息，请参阅源。

- `bpr_initialdelay`：更改`mInitialDelay`，即与设备进行任何通信之前的延迟（以毫秒为单位）。预设值为`100`
- `bpr_handshake`：覆盖`mSupportsHandshake`，固件上传的握手支持状态。` 0`表示在上传固件后等待`bpr_preresetdelay` 毫秒，然后重置设备。 `1`表示等待来自设备的特定响应，然后重置设备。默认值取决于设备标识符。
- `bpr_preresetdelay`：更改`mPreResetDelay`，即设备接受固件所需的延迟（以毫秒为单位）。当`bpr_handshake`为`1`（根据设备标识符手动传递或自动应用）时，该值未使用。默认值为`250`
- `bpr_postresetdelay`：更改`mPostResetDelay`，即固件上传后重置设备后，固件初始化所需的延迟（以毫秒为单位）。预设值为`100`
- `bpr_probedelay`：更改`mProbeDelay`（已在BrcmPatchRAM3中删除），即探测设备之前的延迟（以毫秒为单位）。预设值为`0`

例如，要将`mPostResetDelay`更改为400ms，请使用内核标志：`bpr_postresetdelay=400`。

注意：一些典型的“从睡眠中唤醒”问题报告成功：`bpr_probedelay=100 bpr_initialdelay=300 bpr_postresetdelay=300`。 或稍长的延迟：`bpr_probedelay=200 bpr_initialdelay=400 bpr_postresetdelay=400`。

### 细节

`BrcmPatchRAM`包含2个部分：

  * `BrcmPatchRAM`本身与受支持的Broadcom蓝牙USB设备（在`Info.plist`中配置）进行通信，并检测它们是否需要固件更新。

   如果需要固件更新，则匹配的固件数据将被上载到设备并重置设备。
	

 * `BrcmFirmwareStore`（由`BrcmFirmwareData.kext`或`BrcmFirmwareRepo.kext`实现）是共享资源，其中包含用于不同Broadcom蓝牙USB设备的所有已配置固件。

   某些设备需要特定于设备的固件，而其他设备可以使用Windows驱动程序中可用的最新版本。

   会定期添加/配置新固件以支持设备，因此请确保遵循发行更新，或者如果发现不支持设备，请记录问题。

	可以使用zlib压缩存储固件，以使配置大小易于管理。

上传设备固件后，设备控件将移交给Apple的`BroadcomBluetoothHostControllerUSBTransport`。
这意味着，出于所有意图和目的，您的设备将是macOS上的本地设备，并且完全支持所有功能。

可以通过引导加载程序或通过BrcmPatchRAM与Continuity Activation Patch结合使用 [BT4LEContinuityFixup](https://github.com/acidanthera/BT4LEContinuityFixup), 或通过dokterdok的脚本 [Continuity-Activation-Tool](https://github.com/dokterdok/Continuity-Activation-Tool)  

[OpenCore](https://github.com/acidanthera/OpenCorePkg) 用户可以使用`config.plist`中的quirk参数 `ExtendBTFeatureFlags`进行修补。

### 故障排除

安装`BrcmPatchRAM`之后，即使您的蓝牙图标可能出现，也可能是固件未正确更新。

通过转到系统信息并在蓝牙信息面板下检查蓝牙固件版本号来验证固件是否已更新。

如果版本号为` 4096`，则意味着您的设备没有更新固件，并且将无法正常工作。

通过在终端中运行以下命令来验证系统日志中的任何错误：

```bash
    # 10.12或者更新的系统:
    log show --last boot | grep -i brcm[fp]
    # 对于旧的macOS版本:
    cat /var/log/system.log | grep -i brcm[fp]
```
确保只检查最新的引导消息，因为`system.log`可能会追溯几天。

如果固件上传失败并显示错误，请尝试安装`BrcmPatchRAM`的``debug`版本，以便在日志中获取更多详细信息。

为了报告错误，请在github上用以下信息记录问题：

 * Device product ID
 * Device vendor ID
 * 使用的`BrcmPatchRAM`版本
 * `/var/log/system.log`中的`BrcmPatchRAM`调试输出转储，显示固件上传失败

### 固件兼容性

某些USB设备专用于固件，尝试将同一芯片组的任何其他固件上载到它们中都会失败。

通常在系统日志中显示为：
```bash
	BrcmPatchRAM: Version 0.5 starting.
	BrcmPatchRAM: USB [0a5c:21e8 5CF3706267E9 v274] "BCM20702A0" by "Broadcom Corp"
	BrcmPatchRAM: Retrieved firmware for firmware key "BCM20702A1_001.002.014.1443.1612_v5708".
	BrcmPatchRAM: Decompressed firmware (29714 bytes --> 70016 bytes).
	BrcmPatchRAM: device request failed (0xe000404f).
	BrcmPatchRAM: Failed to reset the device (0xe00002d5).
	BrcmPatchRAM: Unable to get device status (0xe000404f).
	BrcmPatchRAM: Firmware upgrade completed successfully.
```

两次之间的错误表示固件未成功上传，并且设备很可能需要配置特定的固件。

对于其他设备，可用的最新固件（即使未在Windows驱动程序中专门指定）也可以正常工作。

### 新设备

为了支持新设备，需要从现有Windows驱动程序中提取该设备的固件。

可在以下位置找到最新（最新）的Broadcom USB蓝牙驱动程序的副本：
http://drivers.softpedia.com/get/BLUETOOTH/Broadcom/ASUS-X99-DELUXE-Broadcom-Bluetooth-Driver-6515800-12009860.shtml#download

*如果您遇到的驱动程序比12.0.0.9860更新，请告诉我。*

为了获取设备专用的设备固件，请执行以下步骤：

 * 查找您的USB设备供应商和产品ID，在此示例中，我们将使用BCM94352Z PCI NGFF WiFi/BT组合卡，其供应商为0930，产品ID为0233。
 * 解压缩Windows蓝牙驱动程序包并打开bcbtums-win8x64-brcm.inf文件
 * 在.inf文件中找到您的供应商/设备ID组合
```dosini
%BRCM20702.DeviceDesc%=BlueRAMUSB0223, USB\VID_0930&PID_0223       ; 20702A1 Toshiba 4352
```
 * 在.inf文件中找到提到的`RAMUSB0223`设备：
```dosini
;;;;;;;;;;;;;RAMUSB0223;;;;;;;;;;;;;;;;;
[RAMUSB0223.CopyList]
bcbtums.sys
btwampfl.sys
BCM20702A1_001.002.014.1443.1457.hex
```

 *  在这种情况下，请从Windows软件包中复制与设备匹配的固件十六进制文件。`BCM20702A1_001.002.014.1443.1457.hex`
	
 *  现在可以选择使用随附的zlib.pl脚本压缩固件文件：
```bash	
zlib.pl deflate BCM20702A1_001.002.014.1443.1457.hex > BCM20702A1_001.002.014.1443.1457.zhx
```
 * 之后，可以创建一个十六进制转储，以粘贴到plist编辑器中：
```bash	
xxd -ps BCM20702A1_001.002.014.1443.1457.zhx|tr '\n' ' ' > BCM20702A1_001.002.014.1443.1457.dmp
```
 * *使用plist编辑器在* *`BcmFirmwareStore/Firmwares`*词典下创建一个新的固件密钥。

      请注意，macOS中显示的版本号是文件名中的最后一个数字（在我们的示例中为1457）+ 4096。

      因此，在这种情况下，macOS中的固件版本为：*`c14 v5553`*。	

 * 在*`BcmFirmwareStore/Firmwares`*下配置密钥后，将设备ID添加为`BrcmPatchRAM`的新设备。

固件也可以直接通过`BrcmFirmwareRepo.kext/Contents/Resources`加载，可以通过固件密钥名称（请参见上文），也可以仅使用供应商和设备ID命名文件。 例如，`0930_0223.hex`（未压缩）或`0930_0223.zhx`（压缩）。

 复制现有的IOKit个性化并修改其属性是最简单的方法。
  使用其唯一的固件密钥配置较早的固件。

### 支持和讨论

[InsanelyMac topic](https://www.insanelymac.com/forum/topic/339175-brcmpatchram2-for-1015-catalina-broadcom-bluetooth-firmware-upload/) in English  
[AppleLife topic](https://applelife.ru/threads/bluetooth.2944352/) in Russian  

