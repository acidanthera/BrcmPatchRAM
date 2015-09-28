#!/usr/bin/ruby

require 'base64'
require 'fileutils'
require 'optparse'
require 'ostruct'
require 'zlib'
require 'rexml/document'
include REXML

def add_element(parent, name, text)
  element = Element.new(name, parent)
  element.text = text
end

def add_key_value(dict, key, type, value)
  add_element(dict, "key", key)
  add_element(dict, type, value)
end

def parse_inf(inf_path)
  devices = Array.new
  in_device_block = 0
  device = nil
  
  if !File.exist?(inf_path)
    puts "Error: bcbtums-win8x64-brcm.inf not found in firmware input folder."
    exit
  end

  File.open(inf_path).each do |line|

    # When in the device block, parse all devices
    if in_device_block == 1 and line =~ /^%([\w\.]*)%=Blue(\w*),\s*USB\\VID_([0-9A-F]{4})\&PID_([0-9A-F]{4})\s*;\s(.*?)\r\n$/
      # Example match: %BRCM20702.DeviceDesc%=BlueRAMUSB21E8,          USB\VID_0A5C&PID_21E8       ; 20702A1 dongles
      device = OpenStruct.new
    
      device.stringKey = $1
      device.deviceKey = $2
      device.vendorId  = $3.hex
      device.productId = $4.hex
      device.comment   = $5
    
      devices << device
    
      device = nil
    end
  
    # Extract the firmware file name from the current device block
    if device and line =~ /^(BCM.*\.hex)/
      device.firmware = $1
      device.firmwareVersion = $1[-8..-1].chomp(".hex").to_i + 4096
      device = nil
    end
  
    # Determine the firmware filename for each device
    if line =~ /^\[(RAMUSB[0-9A-F]{4})\.CopyList\]/
      # Example match: [RAMUSB21E8.CopyList]
      #/^\[(RAMUSB[0-9A-F]{4})\.CopyList\]$/

      # Locate the device information for this RAMUSB device in the firmware array
      device = devices.find { |f| f.deviceKey.casecmp($1) == 0 }
    end
  
    # Extract device descriptions
    if line =~ /^(\w*\.DeviceDesc)\=\s*"(.*)"/
      matches = devices.each.select { |f| f.stringKey.casecmp($1) == 0 }
    
      matches.each do |match|
        match.description = $2
      end
    end

    # Found start of Windows 10 drivers block
    if line =~ /^\[Broadcom\.NTamd64\.10\.0\]/
      in_device_block = 1
    end
    
    # Found end of Windows 10 drivers block
    if line =~ /^\[Broadcom\.NTamd64\.6\.3\]/
      in_device_block = 0
    end
  end
  
  return devices
end

def create_firmwares(devices, input_path, output_path)
  # Create output folder
  FileUtils::makedirs output_path
  FileUtils::chdir output_path
  
  # Wipe any existing symbolic links - Ignore exceptions
  begin
    FileUtils::remove(Dir.glob(File.join(output_path, "*.zhx")))
  rescue
  end

  # Prune and rename existing firmwares
  Dir.glob(File.join(input_path, "*.hex")).each do |firmware|
    basename = File.basename(firmware)
  
    # Validate if we have a matching firmware definition
    device = devices.find { |d| d.firmware.casecmp(basename) == 0 }
  
    if device  
      output_file = "#{File.basename(firmware, File.extname(firmware))}_v#{device.firmwareVersion}.zhx"
      data_to_compress = File.read(firmware)
      data_compressed = Zlib::Deflate.deflate(data_to_compress, Zlib::BEST_COMPRESSION)
    
      puts "Compressed firmware #{output_file} (#{data_to_compress.size} --> #{data_compressed.size})"
    
      device_folder = "%04x_%04x" % [ device.vendorId, device.productId ]
      device_path = File.join(output_path, device_folder)
    
      FileUtils::makedirs(device_path)
      File.write(File.join(device_path, output_file), data_compressed)
      
      # Determine latest firmware for the current device and symlink
      latest_firmware = Dir.glob(File.join(device_path, "*.zhx")).sort_by{ |f| f[-8..1] }.reverse.each.first
      
      if File.exist?(File.basename(latest_firmware))
        puts "Firmware symlink #{File.basename(latestfirmware)} already created for another device."
      else  
        FileUtils::symlink("./" + File.join(device_folder, File.basename(latest_firmware)), File.basename(latest_firmware))
      end
      
      create_injector(device, data_compressed, device_path)
    else
      puts "Firmware file %s is not matched against devices in INF file... skipping." % basename
    end
  end
end

def create_injector(device, compressed_data, output_path)
  xml = Document.new('<?xml version="1.0" encoding="UTF-8"?><!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd"><plist version="1.0"/>');
  
  root_dict = Element.new("dict", xml.root)
  add_key_value(root_dict, "CFBundleIdentifier", "string", "com.no-one.BrcmInjector.%04x.%04x" % [ device.vendorId, device.productId ])
  add_key_value(root_dict, "CFBundleInfoDictionaryVersion", "string", "6.0")
  add_key_value(root_dict, "CFBundleName", "string", "BrcmInjector.%04x.%04x" % [ device.vendorId, device.productId ])
  add_key_value(root_dict, "CFBundlePackageType", "string", "KEXT")
  add_key_value(root_dict, "CFBundleShortVersionString", "string", "2.1.0")
  add_key_value(root_dict, "CFBundleSignature", "string", "????")
  add_key_value(root_dict, "CFBundleVersion", "string", "2.1.0")
  
  add_element(root_dict, "key", "IOKitPersonalities")
  iokit_dict = Element.new("dict", root_dict)
  
  add_element(iokit_dict, "key", "%04x_%04x" % [ device.vendorId, device.productId ])
  
  device_dict = Element.new("dict", iokit_dict);
  add_key_value(device_dict, "CFBundleIdentifier", "string", "com.no-one.BrcmPatchRAM2")
  add_key_value(device_dict, "DisplayName", "string", device.description)
  add_key_value(device_dict, "FirmwareKey", "string", "%04x_%04x_v%4d" % [ device.vendorId, device.productId, device.firmwareVersion ])
  add_key_value(device_dict, "IOClass", "string", "BrcmPatchRAM2")
  add_key_value(device_dict, "IOMatchCategory", "string", "BrcmPatchRAM2")
  add_key_value(device_dict, "IOProbeScore", "integer", "2000")
  add_key_value(device_dict, "IOProviderClass", "string", "IOUSBHostDevice")
  add_key_value(device_dict, "idProduct", "integer", device.productId.to_i())
  add_key_value(device_dict, "idVendor", "integer", device.vendorId.to_i())
  
  add_element(iokit_dict, "key", "BrcmFirmwareStore")
  
  firmware_dict = Element.new("dict", iokit_dict)
  add_key_value(firmware_dict, "CFBundleIdentifier", "string", "com.no-one.BrcmFirmwareStore")
  add_element(firmware_dict, "key", "Firmwares")
  
  firmwares_dict = Element.new("dict", firmware_dict)
  add_key_value(firmwares_dict,  "%04x_%04x_v%4d" % [ device.vendorId, device.productId, device.firmwareVersion ], "data", Base64.encode64(compressed_data))
  
  add_key_value(firmware_dict, "IOClass", "string", "BrcmFirmwareStore")
  add_key_value(firmware_dict, "IOMatchCategory", "string", "BrcmFirmwareStore")
  add_key_value(firmware_dict, "IOProbeScore", "integer", "2000")
  add_key_value(firmware_dict, "IOProviderClass", "string", "disabled_IOResources")
  
  injector_path = File.join(output_path, "BrcmFirmwareInjector_%04x_%04x.kext/Contents" % [ device.vendorId, device.productId, device.version ])
  
  FileUtils::makedirs(injector_path)
  
  formatter = REXML::Formatters::Pretty.new
  formatter.compact = true
  File.open(File.join(injector_path, "Info.plist"), "w") { |file| file.puts formatter.write(xml.root, "") }
end

def create_plist(devices, output_path)
  # Generate plist XML snippet
  xml = Document.new "<dict />"

  devices.sort_by{|d| [d.vendorId, d.productId]}.each do |device|
    Element.new("key", xml.root).text = "%04x_%04x" % [ device.vendorId, device.productId ]
  
    device_xml = Element.new("dict", xml.root)
  
    add_key_value(device_xml, "CFBundleIdentifier", "string", "com.no-one.$(PRODUCT_NAME:rfc1034identifier)")
    add_key_value(device_xml, "DisplayName", "string", device.description)
    add_key_value(device_xml, "FirmwareKey", "string", "#{device.firmware.chomp(".hex")}_v#{device.firmwareVersion}")
    add_key_value(device_xml, "IOClass", "string", "BrcmPatchRAM")
    add_key_value(device_xml, "IOMatchCategory", "string", "BrcmPatchRAM")
    add_key_value(device_xml, "IOProviderClass", "string", "IOUSBDevice")
    add_key_value(device_xml, "idProduct", "integer", device.productId)
    add_key_value(device_xml, "idVendor", "integer", device.vendorId)
  end

  formatter = REXML::Formatters::Pretty.new
  formatter.compact = true
  File.open(File.join(output_path, "firmwares.plist"), "w") { |file| file.puts formatter.write(xml.root, "") }
end

if ARGV.length != 2
  puts "Usage: firmware.rb <input folder> <output folder>"
  exit
end

input = File.expand_path(ARGV.shift)
output = File.expand_path(ARGV.shift)

# Parse Windows INF file into device objects
devices = parse_inf(File.join(input, "bcbtums-win8x64-brcm.inf"))

# Extract and compress all device firmwares
create_firmwares(devices, input, output)

# 
create_plist(devices, output)