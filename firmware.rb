#!/usr/bin/ruby

require 'fileutils'
require 'optparse'
require 'ostruct'
require 'zlib'
require 'rexml/document'
include REXML

def add_xml(dict, key, type, value)
  element = Element.new("key", dict)
  element.text = key
  
  element = Element.new(type, dict)
  element.text = value
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
  FileUtils::mkdir_p output_path

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
    
      FileUtils::mkdir_p(File.join(output_path, "%04x_%04x" % [ device.vendorId, device.productId ]))
      File.write(File.join(output_path, "%04x_%04x" % [ device.vendorId, device.productId ], output_file), data_compressed)
    else
      puts "Firmware file %s is not matched against devices in INF file... skipping." % basename
    end
  end
end

def create_plist(devices, output_path)
  # Generate plist XML snippet
  xml = Document.new "<dict />"

  devices.sort_by{|d| [d.vendorId, d.productId]}.each do |device|
    Element.new("key", xml.root).text = "%04x_%04x" % [ device.vendorId, device.productId ]
  
    device_xml = Element.new("dict", xml.root)
  
    add_xml(device_xml, "CFBundleIdentifier", "string", "com.no-one.$(PRODUCT_NAME:rfc1034identifier)")
    add_xml(device_xml, "DisplayName", "string", device.description)
    add_xml(device_xml, "FirmwareKey", "string", "#{device.firmware.chomp(".hex")}_v#{device.firmwareVersion}")
    add_xml(device_xml, "IOClass", "string", "BrcmPatchRAM")
    add_xml(device_xml, "IOMatchCategory", "string", "BrcmPatchRAM")
    add_xml(device_xml, "IOProviderClass", "string", "IOUSBDevice")
    add_xml(device_xml, "idProduct", "integer", device.productId)
    add_xml(device_xml, "idVendor", "integer", device.vendorId)
  end

  formatter = REXML::Formatters::Pretty.new
  formatter.compact = true
  File.open(File.join(output_path, "firmwares.plist"), "w") { |file| file.puts formatter.write(xml.root, "") }
end

if ARGV.length != 2
  puts "Usage: firmware.rb <input folder> <output folder>"
  exit
end

input = ARGV.shift
output = ARGV.shift

# Parse Windows INF file into device objects
devices = parse_inf(File.join(input, "bcbtums-win8x64-brcm.inf"))

# Extract and compress all device firmwares
create_firmwares(devices, input, output)

# 
create_plist(devices, output)