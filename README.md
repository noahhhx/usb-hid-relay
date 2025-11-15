# usb-hid-relay

## tldr;
Fuck League of Legends, fuck Riot and fuck Vanguard.

Relay mouse events from Linux to Windows, over UDP via Pi acting as a HID device.

## Motivations
I use Linux for everything in my life other than League of Legends, so I have an old PC setup as a
server for League of Legends. For this I use [Sunshine](link here) on the Windows PC to host and
[Moonlight](link here) on the Linux machine to remote in. Since everything is wired locally, this
works with virtually 0 noticeable latency. The problem is the shitty abomination that Vanguard is
blocks the virtual mouse that Sunshine + Moonlight uses.

This project is an attempt to fix this.

## Demo
![Demo](./demo.gif)

## Setup/Environment
- Windows 11 PC
  - [Sunshine](https://github.com/LizardByte/Sunshine)
  - [HDMI dummy plug](https://www.amazon.co.uk/dp/B087QDCZ4L)
  - [Auto login](https://www.stellarinfo.com/article/windows-11-auto-login.php) - nice if/when we have to reboot
- Framework Laptop
  - [Arch](https://archlinux.org/)
  - [Hyprland](https://hypr.land/) !IMPORTANT! - USB Host uses `hyprctl` for getting active windows.
  - [Moonlight](https://github.com/moonlight-stream)
- Raspberry Pi Zero 2 w
  - ENC28J60 & Dupont wires optionally for lower latency
  - MicroUSB to USB

## Building
Build USB Host native:
```shell
gcc -o usb_host usb_host.c window_monitor.c # for linux machine
```

Install ARM cross-compiler:
```shell
sudo apt-get install gcc-aarch64-linux-gnu  # for Pi (64-bit OS)
```

Cross-compile for ARM:
```shell
aarch64-linux-gnu-gcc -o usb_client usb_client.c  # for Pi (64-bit)
```

## Raspberry Pi setup
Need to setup the Pi up to act as a USB host, so Windows recognizes it as a mouse.

---

Enable modules and drivers:
```shell
echo "dtoverlay=dwc2" | sudo tee -a /boot/config.txt
sudo echo "dwc2" | sudo tee -a /etc/modules
sudo echo "libcomposite" | sudo tee -a /etc/modules
```
Double check:
```shell
sudo nano /boot/firmware/config.txt
```
and add dtoverlay if it's not under [all]
```
[all]
dtoverlay=dwc2
```

Reboot
```shell
sudo reboot
lsmod | grep dwc2 # Should now show dwc2
```

---

Create config scripts:

```shell
sudo touch /usr/bin/pi_usb
sudo chmod +x /usr/bin/pi_usb
```

```shell
sudo nano /usr/bin/pi_usb
```
and enter, which creates the USB gadget when ran:
```text
#!/bin/bash

GADGET_DIR="/sys/kernel/config/usb_gadget/usb_pi"

# Cleanup function
cleanup_gadget() {
    if [ -d "$GADGET_DIR" ]; then
        echo "Cleaning up existing gadget..."
        cd "$GADGET_DIR"
        
        # Unbind from UDC (important to do first!)
        if [ -f UDC ]; then
            echo "" > UDC 2>/dev/null || true
        fi
        
        # Remove symlinks from configs
        find configs/*/hid.usb* -type l -delete 2>/dev/null || true
        
        # Remove functions
        find functions/hid.usb* -type d -exec rmdir {} \; 2>/dev/null || true
        
        # Remove config strings
        find configs/*/strings/* -type d -exec rmdir {} \; 2>/dev/null || true
        
        # Remove configs
        find configs/* -type d -exec rmdir {} \; 2>/dev/null || true
        
        # Remove gadget strings
        find strings/* -type d -exec rmdir {} \; 2>/dev/null || true
        
        # Remove the gadget itself
        cd /sys/kernel/config/usb_gadget/
        rmdir usb_pi 2>/dev/null || true
        
        echo "Cleanup complete"
        sleep 1
    fi
}

# Load required modules
modprobe libcomposite

# Clean up first
cleanup_gadget

# Create fresh gadget
cd /sys/kernel/config/usb_gadget/
mkdir -p usb_pi
cd usb_pi

echo 0x1d6b > idVendor
echo 0x0104 > idProduct
echo 0x0100 > bcdDevice
echo 0x0200 > bcdUSB

mkdir -p strings/0x409
echo "fedcba9876543210" > strings/0x409/serialnumber
echo "Noah Giles" > strings/0x409/manufacturer
echo "USB Mouse" > strings/0x409/product

mkdir -p configs/c.1/strings/0x409
echo "Config 1: HID Mouse" > configs/c.1/strings/0x409/configuration
echo 250 > configs/c.1/MaxPower

# Add HID mouse function
mkdir -p functions/hid.usb0
echo 2 > functions/hid.usb0/protocol
echo 1 > functions/hid.usb0/subclass
echo 4 > functions/hid.usb0/report_length
echo -ne \\x05\\x01\\x09\\x02\\xa1\\x01\\x09\\x01\\xa1\\x00\\x05\\x09\\x19\\x01\\x29\\x05\\x15\\x00\\x25\\x01\\x95\\x05\\x75\\x01\\x81\\x02\\x95\\x01\\x75\\x03\\x81\\x03\\x05\\x01\\x09\\x30\\x09\\x31\\x15\\x81\\x25\\x7f\\x75\\x08\\x95\\x02\\x81\\x06\\x09\\x38\\x15\\x81\\x25\\x7f\\x75\\x08\\x95\\x01\\x81\\x06\\xc0\\xc0 > functions/hid.usb0/report_desc

ln -s functions/hid.usb0 configs/c.1/

# Bind to UDC
UDC_DEVICE=$(ls /sys/class/udc | head -n1)
echo "$UDC_DEVICE" > UDC

echo "USB HID Mouse gadget configured successfully!"
echo "Device: $UDC_DEVICE"
echo "HID device should be at /dev/hidg0"
```

---

Create Systemd service, to auto-start if the Pi reboots.
```shell
sudo nano /etc/systemd/system/pi_usb.service
```
with the following contents:
```text
[Unit]
Description=Run my custom script at startup
After=network.target

[Service]
Type=oneshot
ExecStart=/usr/bin/pi_usb
RemainAfterExit=yes
User=root

[Install]
WantedBy=multi-user.target
```

and to start the service:
```shell
sudo systemctl daemon-reload
sudo systemctl enable pi_usb.service
sudo systemctl start pi_usb.service
```

---

Check it worked:
```shell
# Should show a UDC device
ls /sys/class/udc/
# Should exist now
ls -l /dev/hidg0
```
If they don't show, gg figure it out.

## Running
You should have:
- usb_host - Intended for Linux (sending mouse events to Pi)
- usb_client - Intended for Raspberry Pi (receiving mouse events from Linux and forward to Windows)

---

### Pi
Copy usb_client to Pi:
```shell
scp usb_client noah@usbpi.local:/home/noah/
```
and run using:
```shell
sudo ./usb_client
```
We should see two log lines for opening the HID device and listening to port 5555.

--- 

### Linux

Find mouse:
```shell
cat /proc/bus/input/devices
```
You are looking for the event number of your mouse.

Then run:
```shell
sudo -E ./usb_host -d /dev/input/event5 -i 192.168.0.102 -w stream.Moonlight -s 1.677
```
`sudo -E` or `sudo HYPRLAND_INSTANCE_SIGNATURE=$HYPRLAND_INSTANCE_SIGNATURE` so we can use hyprctl. \
`event5` = mouse event number \
`192.168.0.102` = IP of Pi \
`stream.Moonlight` = Class of window (I only want to send mouse events when Moonlight is active) \
`1.677` = Sensitivity scaling`
