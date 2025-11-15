#!/usr/bin/env bash
# Configure Raspberry Pi USB HID gadget as a 4-byte mouse (Buttons + X + Y + Wheel)
# This matches the usb_client which writes 4-byte reports.
set -euo pipefail

GADGET_BASE="/sys/kernel/config/usb_gadget"
GADGET_NAME="usb_pi"
GADGET_DIR="$GADGET_BASE/$GADGET_NAME"

cleanup_gadget() {
  if [ -d "$GADGET_DIR" ]; then
    echo "[pi_usb] Cleaning up existing gadget..."
    cd "$GADGET_DIR"

    # Unbind from UDC first
    if [ -f UDC ]; then
      echo "" > UDC 2>/dev/null || true
    fi

    # Remove function links from configs
    find configs/*/hid.usb* -type l -delete 2>/dev/null || true

    # Remove functions
    find functions/hid.usb* -maxdepth 0 -type d -exec rmdir {} \; 2>/dev/null || true

    # Remove config strings
    find configs/*/strings/* -maxdepth 0 -type d -exec rmdir {} \; 2>/dev/null || true

    # Remove configs
    find configs/* -maxdepth 0 -type d -exec rmdir {} \; 2>/dev/null || true

    # Remove gadget strings
    find strings/* -maxdepth 0 -type d -exec rmdir {} \; 2>/dev/null || true

    # Remove the gadget itself
    cd "$GADGET_BASE"
    rmdir "$GADGET_NAME" 2>/dev/null || true

    echo "[pi_usb] Cleanup complete"
    sleep 1
  fi
}

main() {
  # Ensure configfs/composite are available
  modprobe libcomposite || true
  if [ ! -d "$GADGET_BASE" ]; then
    echo "[pi_usb] ERROR: $GADGET_BASE not mounted. Is configfs enabled?" >&2
    exit 1
  fi

  # Clean any existing gadget
  cleanup_gadget

  # Create gadget
  mkdir -p "$GADGET_DIR"
  cd "$GADGET_DIR"

  echo 0x1d6b > idVendor      # Linux Foundation (example)
  echo 0x0104 > idProduct     # Multifunction Composite Gadget (example)
  echo 0x0100 > bcdDevice
  echo 0x0200 > bcdUSB

  mkdir -p strings/0x409
  echo "fedcba9876543210" > strings/0x409/serialnumber
  echo "Noah Giles" > strings/0x409/manufacturer
  echo "USB Mouse" > strings/0x409/product

  mkdir -p configs/c.1/strings/0x409
  echo "Config 1: HID Mouse" > configs/c.1/strings/0x409/configuration
  echo 250 > configs/c.1/MaxPower

  # HID mouse function (4-byte report: buttons, X, Y, Wheel)
  mkdir -p functions/hid.usb0
  echo 2 > functions/hid.usb0/protocol   # Mouse
  echo 1 > functions/hid.usb0/subclass   # Boot
  echo 4 > functions/hid.usb0/report_length
  # Report descriptor: Buttons (3), Padding (5), X, Y (rel), Wheel (rel)
  echo -ne \
"\x05\x01"      # Usage Page (Generic Desktop)\
"\x09\x02"      # Usage (Mouse)\
"\xA1\x01"      # Collection (Application)\
"\x09\x01"      #   Usage (Pointer)\
"\xA1\x00"      #   Collection (Physical)\
"\x05\x09"      #     Usage Page (Button)\
"\x19\x01"      #     Usage Minimum (Button 1)\
"\x29\x03"      #     Usage Maximum (Button 3)\
"\x15\x00"      #     Logical Minimum (0)\
"\x25\x01"      #     Logical Maximum (1)\
"\x95\x03"      #     Report Count (3)\
"\x75\x01"      #     Report Size (1)\
"\x81\x02"      #     Input (Data,Var,Abs) - buttons\
"\x95\x01"      #     Report Count (1)\
"\x75\x05"      #     Report Size (5)\
"\x81\x03"      #     Input (Const,Var,Abs) - padding\
"\x05\x01"      #     Usage Page (Generic Desktop)\
"\x09\x30"      #     Usage (X)\
"\x09\x31"      #     Usage (Y)\
"\x15\x81"      #     Logical Minimum (-127)\
"\x25\x7f"      #     Logical Maximum (127)\
"\x75\x08"      #     Report Size (8)\
"\x95\x02"      #     Report Count (2)\
"\x81\x06"      #     Input (Data,Var,Rel) - X, Y\
"\x09\x38"      #     Usage (Wheel)\
"\x15\x81"      #     Logical Minimum (-127)\
"\x25\x7f"      #     Logical Maximum (127)\
"\x75\x08"      #     Report Size (8)\
"\x95\x01"      #     Report Count (1)\
"\x81\x06"      #     Input (Data,Var,Rel) - Wheel\
"\xC0"          #   End Collection (Physical)\
"\xC0"          # End Collection (Application)\
  > functions/hid.usb0/report_desc

  ln -s functions/hid.usb0 configs/c.1/

  # Bind to UDC
  UDC_DEVICE=$(ls /sys/class/udc | head -n1)
  echo "$UDC_DEVICE" > UDC

  echo "[pi_usb] USB HID Mouse gadget configured successfully!"
  echo "[pi_usb] Device: $UDC_DEVICE"
  echo "[pi_usb] HID device should be at /dev/hidg0"
}

main "$@"
