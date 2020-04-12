#!/bin/sh
#if [ $# -ne 2 ];then
#    echo "Usage: uvc_MJPEG.sh width height"
#    echo "e.g. uvc_MJPEG.sh 640 480"
#    exit 0
#fi
USB_FUNCTIONS_DIR=/sys/kernel/config/usb_gadget/rockchip/functions
configure_uvc_resolution_yuyv()
{
        UVC_DISPLAY_W=$1
        UVC_DISPLAY_H=$2
        mkdir ${USB_FUNCTIONS_DIR}/uvc.gs6/streaming/uncompressed/u/${UVC_DISPLAY_H}p
        echo $UVC_DISPLAY_W > ${USB_FUNCTIONS_DIR}/uvc.gs6/streaming/uncompressed/u/${UVC_DISPLAY_H}p/wWidth
        echo $UVC_DISPLAY_H > ${USB_FUNCTIONS_DIR}/uvc.gs6/streaming/uncompressed/u/${UVC_DISPLAY_H}p/wHeight
        echo 333333 > ${USB_FUNCTIONS_DIR}/uvc.gs6/streaming/uncompressed/u/${UVC_DISPLAY_H}p/dwDefaultFrameInterval
        echo $((UVC_DISPLAY_W*UVC_DISPLAY_H*80)) > ${USB_FUNCTIONS_DIR}/uvc.gs6/streaming/uncompressed/u/${UVC_DISPLAY_H}p/dwMinBitRate
        echo $((UVC_DISPLAY_W*UVC_DISPLAY_H*160)) > ${USB_FUNCTIONS_DIR}/uvc.gs6/streaming/uncompressed/u/${UVC_DISPLAY_H}p/dwMaxBitRate
        echo $((UVC_DISPLAY_W*UVC_DISPLAY_H*2)) > ${USB_FUNCTIONS_DIR}/uvc.gs6/streaming/uncompressed/u/${UVC_DISPLAY_H}p/dwMaxVideoFrameBufferSize
        echo -e "333333\n666666\n1000000\n2000000" > ${USB_FUNCTIONS_DIR}/uvc.gs6/streaming/uncompressed/u/${UVC_DISPLAY_H}p/dwFrameInterval
}

configure_uvc_resolution_mjpeg()
{
	UVC_DISPLAY_W=$1
	UVC_DISPLAY_H=$2
	mkdir ${USB_FUNCTIONS_DIR}/uvc.gs6/streaming/mjpeg/m/${UVC_DISPLAY_H}p
	echo $UVC_DISPLAY_W > ${USB_FUNCTIONS_DIR}/uvc.gs6/streaming/mjpeg/m/${UVC_DISPLAY_H}p/wWidth
	echo $UVC_DISPLAY_H > ${USB_FUNCTIONS_DIR}/uvc.gs6/streaming/mjpeg/m/${UVC_DISPLAY_H}p/wHeight
	echo 333333 > ${USB_FUNCTIONS_DIR}/uvc.gs6/streaming/mjpeg/m/${UVC_DISPLAY_H}p/dwDefaultFrameInterval
	echo $((UVC_DISPLAY_W*UVC_DISPLAY_H*80)) > ${USB_FUNCTIONS_DIR}/uvc.gs6/streaming/mjpeg/m/${UVC_DISPLAY_H}p/dwMinBitRate
	echo $((UVC_DISPLAY_W*UVC_DISPLAY_H*160)) > ${USB_FUNCTIONS_DIR}/uvc.gs6/streaming/mjpeg/m/${UVC_DISPLAY_H}p/dwMaxBitRate
	echo $((UVC_DISPLAY_W*UVC_DISPLAY_H*2)) > ${USB_FUNCTIONS_DIR}/uvc.gs6/streaming/mjpeg/m/${UVC_DISPLAY_H}p/dwMaxVideoFrameBufferSize
	echo -e "333333\n666666\n1000000\n2000000" > ${USB_FUNCTIONS_DIR}/uvc.gs6/streaming/mjpeg/m/${UVC_DISPLAY_H}p/dwFrameInterval
}
configure_uvc_resolution_h264()
{
	UVC_DISPLAY_W=$1
	UVC_DISPLAY_H=$2
	mkdir ${USB_FUNCTIONS_DIR}/uvc.gs6/streaming/framebased/f/${UVC_DISPLAY_H}p
	echo $UVC_DISPLAY_W > ${USB_FUNCTIONS_DIR}/uvc.gs6/streaming/framebased/f/${UVC_DISPLAY_H}p/wWidth
	echo $UVC_DISPLAY_H > ${USB_FUNCTIONS_DIR}/uvc.gs6/streaming/framebased/f/${UVC_DISPLAY_H}p/wHeight
	echo 333333 > ${USB_FUNCTIONS_DIR}/uvc.gs6/streaming/framebased/f/${UVC_DISPLAY_H}p/dwDefaultFrameInterval
	echo $((UVC_DISPLAY_W*UVC_DISPLAY_H*80)) > ${USB_FUNCTIONS_DIR}/uvc.gs6/streaming/framebased/f/${UVC_DISPLAY_H}p/dwMinBitRate
	echo $((UVC_DISPLAY_W*UVC_DISPLAY_H*160)) > ${USB_FUNCTIONS_DIR}/uvc.gs6/streaming/framebased/f/${UVC_DISPLAY_H}p/dwMaxBitRate
	#echo $((UVC_DISPLAY_W*UVC_DISPLAY_H*2)) > ${USB_FUNCTIONS_DIR}/uvc.gs6/streaming/framebased/f/${UVC_DISPLAY_H}p/dwMaxVideoFrameBufferSize
	echo -e "333333\n666666\n1000000\n2000000" > ${USB_FUNCTIONS_DIR}/uvc.gs6/streaming/framebased/f/${UVC_DISPLAY_H}p/dwFrameInterval
}

/etc/init.d/S10udev stop

umount /sys/kernel/config
mount -t configfs none /sys/kernel/config
mkdir -p /sys/kernel/config/usb_gadget/rockchip
mkdir -p /sys/kernel/config/usb_gadget/rockchip/strings/0x409
mkdir -p /sys/kernel/config/usb_gadget/rockchip/configs/b.1/strings/0x409

echo 0x2207 > /sys/kernel/config/usb_gadget/rockchip/idVendor
echo 0x0310 > /sys/kernel/config/usb_gadget/rockchip/bcdDevice
echo 0x0200 > /sys/kernel/config/usb_gadget/rockchip/bcdUSB

echo "2020" > /sys/kernel/config/usb_gadget/rockchip/strings/0x409/serialnumber
echo "rockchip" > /sys/kernel/config/usb_gadget/rockchip/strings/0x409/manufacturer
echo "UVC" > /sys/kernel/config/usb_gadget/rockchip/strings/0x409/product

if [ "$1"x == "rndis"x  ]; then
   # config rndis
   mkdir /sys/kernel/config/usb_gadget/rockchip/functions/rndis.gs0
fi

mkdir /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6
echo 1024 > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming_maxpacket
cat /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming_maxpacket
#echo 1 > /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming_bulk

mkdir /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/control/header/h
ln -s /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/control/header/h /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/control/class/fs/h
ln -s /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/control/header/h /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/control/class/ss/h
ln -s /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/control/header/h /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/control/class/hs/h

##YUYV support config
#mkdir /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/uncompressed/u
#configure_uvc_resolution_yuyv 640 480
#configure_uvc_resolution_yuyv 1280 720

##mjpeg support config
mkdir /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m
configure_uvc_resolution_mjpeg 640 480
configure_uvc_resolution_mjpeg 1280 720
configure_uvc_resolution_mjpeg 1920 1080
configure_uvc_resolution_mjpeg 2560 1440
configure_uvc_resolution_mjpeg 2592 1944

## h.264 support config
mkdir /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/framebased/f
configure_uvc_resolution_h264 640 480
##configure_uvc_resolution_h264 1280 720
configure_uvc_resolution_h264 1920 1080


mkdir /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/header/h
#ln -s /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/uncompressed/u /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/header/h/u
ln -s /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/mjpeg/m /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/header/h/m
ln -s /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/framebased/f /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/header/h/f
ln -s /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/header/h /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/class/fs/h
ln -s /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/header/h /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/class/hs/h
ln -s /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/header/h /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6/streaming/class/ss/h

echo 0x1 > /sys/kernel/config/usb_gadget/rockchip/os_desc/b_vendor_code
echo "MSFT100" > /sys/kernel/config/usb_gadget/rockchip/os_desc/qw_sign
echo 500 > /sys/kernel/config/usb_gadget/rockchip/configs/b.1/MaxPower
ln -s /sys/kernel/config/usb_gadget/rockchip/configs/b.1 /sys/kernel/config/usb_gadget/rockchip/os_desc/b.1
echo 0x0005 > /sys/kernel/config/usb_gadget/rockchip/idProduct

if [ "$1"x == "rndis"x  ]; then
   echo "uvc_rndis" > /sys/kernel/config/usb_gadget/rockchip/configs/b.1/strings/0x409/configuration
   echo "config uvc and rndis..."
else
   echo "uvc" > /sys/kernel/config/usb_gadget/rockchip/configs/b.1/strings/0x409/configuration
fi

USB_CONFIGS_DIR=/sys/kernel/config/usb_gadget/rockchip/configs/b.1
if [ -e ${USB_CONFIGS_DIR}/ffs.adb ]; then
   #for rk1808 kernel 4.4
   rm -f ${USB_CONFIGS_DIR}/ffs.adb
else
   ls ${USB_CONFIGS_DIR} | grep f[0-9] | xargs -I {} rm ${USB_CONFIGS_DIR}/{}
fi
ln -s /sys/kernel/config/usb_gadget/rockchip/functions/uvc.gs6 /sys/kernel/config/usb_gadget/rockchip/configs/b.1/f1

if [ "$1"x == "rndis"x  ]; then
   ln -s /sys/kernel/config/usb_gadget/rockchip/functions/rndis.gs0 /sys/kernel/config/usb_gadget/rockchip/configs/b.1/f2
fi

UDC=`ls /sys/class/udc/| awk '{print $1}'`
echo $UDC > /sys/kernel/config/usb_gadget/rockchip/UDC

if [ "$1"x == "rndis"x  ]; then
   sleep 1
   echo "config usb0 IP :172.16.110.6 ..."
   ifconfig usb0 172.16.110.6 
   ifconfig usb0 up
fi
