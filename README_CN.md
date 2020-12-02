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

  * `BrcmPatchRAM2.kext`: 适用于 10.11 或更高版本.
  
* `BrcmPatchRAM3.kext`: 适用于 10.15.

另外，根据安装位置安装一个固件kext BrcmFirmwareData.kext或BrcmFirmwareRepo.kext，决不要同时安装。

  * `BrcmFirmwareData.kext`: 最适合引导加载程序注入。 这是首选配置。

  * `BrcmFirmwareRepo.kext`: 安装到`/System/Library/Extensions`（在10.11及更高版本上为`/Library/Extensions`）。 该kext的内存效率比`BrcmFirmwareData.kext`略高，但是不能由引导加载程序注入。

  * 高级用户：对于自定义固件注入器，请安装注入器以及`BrcmFirmwareRepo.kext`。 这可以从`/System/Library/Extensions`或通过引导加载程序注入工作。 （可选）您可以从`BrcmFirmwareRepo.kext/Contents/Resources`中删除所有固件。 如果通过引导加载程序使用注入器，则必须将`BrcmFirmwareRepo.kext`的`Info.plist`中的`IOProviderClass`从`disabled_IOResources`更改为`IOResources`。

另外，如果您有非PatchRAM设备（或者不确定），请安装macOS版本的`BrcmNonPatchRAM.kext`或`BrcmNonPatchRAM2.kext`之一，请不要同时安装两者。尽管这些kext不安装任何固件（这些设备内置固件），但它们仍依赖`BrcmPatchRAM.kext` / `BrcmPatchRAM2.kext`。

  * `BrcmNonPatchRAM.kext`: 适用于 10.10 或更早版本.

  * `BrcmNonPatchRAM2.kext`: 适用于 10.11 或更高版本.

### BrcmBluetoothInjector.kext

用于macOS 10.11或更高版本。

该kext是一个简单的注入器，它不包含固件上载器。 如果您希望查看您的设备在没有固件上传器的情况下是否可以运行，请尝试使用此kext。

请勿将其他任何扩展程序（`BrcmPatchRAM`，`BrcmPatchRAM2`，`BrcmPatchRAM3`，`BrcmFirmwareRepo`或`BrcmFirmwareData`）与此扩展程序一起使用。

> 在`Catalina`以及`Mojave`中可能必须使用`BrcmBluetoothInjector.kext`才能正确驱动蓝牙设备，比如`DW1820A` / `DW1560`

发行版ZIP中未提供此kext。 您可以尝试构建它。 它已被删除，因为它可能会导致那些不太仔细阅读且未正确安装首选kext的人感到困惑。 当前未使用新设备进行更新。 如果您的不存在，请根据需要编辑`Info.plist`。

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
  * ``[0a5c:6414]`` BCM4350C5 (Lenovo) Bluetooth 4.1 LE
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

Windows软件包中的所有固件都存在于kext中，并自动与其供应商/设备ID关联。 它们有望工作，但尚未得到确认。 如果您可以确认上面未列出的工作设备，请通过github上的issues数据库进行通知。

### 更多安装细节

`BrcmPatchRAM.kext`或`BrcmPatchRAM2.kext`或`BrcmPatchRAM3.kext`可以通过引导加载程序kext注入安装，也可以放置在`/System/Library/Extensions`（在10.11及更高版本中为`/Library/Extensions`）中。
根据系统版本，仅安装一个，而不是安装三个。

`BrcmFirmwareRepo.kext`不适用于bootloader kext注入，除非使用特定于设备的固件注入器。
`BrcmFirmwareData.kext`可以与bootloader kext注入一起使用。

您还可以使用特定于设备的固件注入器（与`BrcmFirmwareRepo.kext`结合使用）。 在这种情况下，`BrcmFirmwareRepo.kext`确实可以从引导加载程序kexts中工作。

您可以在git存储库的`firmwares`目录中找到设备专用注射器。 它们不包含在发行版ZIP中。

### 配置

可以通过以下内核标志来更改许多延迟。 如果发现在固件加载期间`BrcmPatchRAM`挂起，则可以更改这些值。

- `bpr_probedelay`：更改`mProbeDelay`。 预设值为`0`

- `bpr_initialdelay`：更改`mInitialDelay`。 预设值为`100`

- `bpr_preresetdelay`：更改`mPreResetDelay`。 预设值为`20`

- `bpr_postresetdelay`：更改`mPostResetDelay`。 预设值为`100`

有关这些延迟的更多详细信息，请参阅来源。

例如，…要将`mPostResetDelay`更改为400ms，请使用内核标志：`bpr_postresetdelay=400`。

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

[OpenCore](https://github.com/acidanthera/OpenCorePkg) 用户可以使用`config.plist`中的内核修补程序进行修补。

10.10的修补程序是：

```XML
<dict>
    <key>Comment</key>
    <string>10.10.2+ BT4LE-Handoff-Hotspot, Dokterdok</string>
    <key>Find</key>
    <data>SIXAdFwPt0g=</data>
    <key>Identifier</key>
    <string>com.apple.iokit.IOBluetoothFamily</string>
    <key>Replace</key>
    <data>Qb4PAAAA61k=</data>
    <!-- Rest of the fields -->
</dict>
```

10.11的修补程序是：

```XML
<dict>
    <key>Comment</key>
    <string>10.11.dp1+ BT4LE-Handoff-Hotspot, credit RehabMan based on Dokterdok original</string>
    <key>Find</key>
    <data>SIX/dEdIiwc=</data>
    <key>Identifier</key>
    <string>com.apple.iokit.IOBluetoothFamily</string>
    <key>Replace</key>
    <data>Qb4PAAAA60Q=</data>
    <!-- Rest of the fields -->
</dict>
```

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

###新设备

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