#!/usr/bin/ruby

require 'fileutils'
require 'optparse'
require 'ostruct'
require 'zlib'
require 'rexml/document'
include REXML

def create_xml(name, value)
  element = Element.new(name)
  element.text = value
  
  return element
end

if ARGV.length != 2
  puts "Usage: firmware.rb <input folder> <output folder>"
  exit
end

input = ARGV.shift
output = ARGV.shift

inf = File.join(input, "bcbtums-win8x64-brcm.inf")
firmwares = Array.new
firmware = nil
devices=0

if !File.exist?(inf)
  puts "Error: bcbtums-win8x64-brcm.inf not found in firmware input folder."
  exit
end

File.open(inf).each do |line|

  # When in the device block, parse all devices
  if devices == 1 and line =~ /^%([\w\.]*)%=Blue(\w*),\s*USB\\VID_([0-9A-F]{4})\&PID_([0-9A-F]{4})\s*;\s(.*?)\r\n$/
    # Example match: %BRCM20702.DeviceDesc%=BlueRAMUSB21E8,          USB\VID_0A5C&PID_21E8       ; 20702A1 dongles
    firmware = OpenStruct.new
    
    firmware.stringKey = $1
    firmware.deviceKey = $2
    firmware.vendorId  = $3.hex
    firmware.productId = $4.hex
    firmware.comment   = $5
    
    firmwares << firmware
    
    firmware = nil
  end
  
  # Extract the firmware file name from the current device block
  if firmware and line =~ /^(BCM.*\.hex)/
    firmware.file = $1
    firmware.version = $1[-8..-1].chomp(".hex").to_i + 4096
    firmware = nil
  end
  
  # Determine the firmware filename for each device
  if line =~ /^\[(RAMUSB[0-9A-F]{4})\.CopyList\]/
    # Example match: [RAMUSB21E8.CopyList]
    #/^\[(RAMUSB[0-9A-F]{4})\.CopyList\]$/

    # Locate the device information for this RAMUSB device in the firmware array
    firmware = firmwares.find { |f| f.deviceKey.casecmp($1) == 0 }
  end
  
  # Extract device descriptions
  if line =~ /^(\w*\.DeviceDesc)\=\s*"(.*)"/
    matches = firmwares.each.select { |f| f.stringKey.casecmp($1) == 0 }
    
    matches.each do |match|
      match.description = $2
    end
  end

  # Found start of Windows 10 drivers block
  if line =~ /^\[Broadcom\.NTamd64\.10\.0\]/
    devices=1
  end
    
  # Found end of Windows 10 drivers block
  if line =~ /^\[Broadcom\.NTamd64\.6\.3\]/
    devices=0
  end
end

# Create output folder
FileUtils::mkdir_p output

# Prune and rename existing firmwares
Dir.glob(File.join(input, "*.hex")).each do |file|
  basename = File.basename(file)
  
  # Validate if we have a matching firmware definition
  firmware = firmwares.find { |f| f.file.casecmp(basename) == 0 }
  
  if firmware  
    output_file = "#{File.basename(file, File.extname(file))}_v#{firmware.version}.zhx"
    data_to_compress = File.read(file)
    data_compressed = Zlib::Deflate.deflate(data_to_compress, Zlib::BEST_COMPRESSION)
    
    puts "Compressed firmware #{output_file} (#{data_to_compress.size} --> #{data_compressed.size})"
    
    FileUtils::mkdir_p(File.join(output, "%04x_%04x" % [ firmware.vendorId, firmware.productId ]))
    File.write(File.join(output, "%04x_%04x" % [ firmware.vendorId, firmware.productId ], output_file), data_compressed)
  else
    puts "Firmware file %s is not matched in INF file... skipping." % basename
  end
end

# Generate plist XML snippet
xml = Document.new "<dict />"

firmwares.sort_by{|f| [f.vendorId, f.productId]}.each do |firmware|
  xml.root.elements << create_xml("key", "%04x_%04x" % [ firmware.vendorId, firmware.productId ])
  
  device = Element.new("dict")
  
  device << create_xml("key", "CFBundleIdentifier")
  device << create_xml("string", "com.no-one.$(PRODUCT_NAME:rfc1034identifier)")
  device << create_xml("key", "DisplayName")
  device << create_xml("string", firmware.description)
  device << create_xml("key", "FirmwareKey")
  device << create_xml("string", "#{firmware.file.chomp(".hex")}_v#{firmware.version}")
  device << create_xml("key", "IOClass")
  device << create_xml("string", "BrcmPatchRAM")
  device << create_xml("key", "IOMatchCategory")
  device << create_xml("string", "BrcmPatchRAM")
  device << create_xml("key", "IOProviderClass")
  device << create_xml("string", "IOUSBDevice")
  device << create_xml("key", "idProduct")
  device << create_xml("integer", firmware.productId)
  device << create_xml("key", "idVendor")
  device << create_xml("integer", firmware.vendorId)
  
  xml.root.elements << device
end

formatter = REXML::Formatters::Pretty.new
formatter.compact = true
File.open(File.join(output, "firmwares.plist"), "w") { |file| file.puts formatter.write(xml.root, "") }
