#!/bin/bash

if ! hash qemu-img 2> /dev/null ; then
   echo "Cannot find qemu-img, make sure it is in your path."
   exit 1
fi

if ! hash qemu-system-x86_64 2> /dev/null ; then
   echo "Cannot find qemu-system-x86_64, make sure it is in your path."
   exit 1
fi


# Download ubuntu-18.04.6-desktop-amd64.iso if it doesn't exist already and if there is no illixr image created
if [ ! -f ubuntu-22.04.2-desktop-amd64.iso ]; then
	if [ ! -f illixr.qcow2 ]; then
    	wget https://releases.ubuntu.com/jammy/ubuntu-22.04.2-desktop-amd64.iso
    fi
fi

# Create a qcow2 image, if one doesn't exist
if [ ! -f illixr.qcow2 ]; then
    qemu-img create -f qcow2 illixr.qcow2 30G
fi

if [ ! -f ubuntu-22.04.2-desktop-amd64.iso ]; then
	# Ubuntu image doesn't exist anymore, so launch without the CDROM option
    qemu-system-x86_64 -enable-kvm -M q35 -smp 2 -m 4G \
        -hda illixr.qcow2 \
        -net nic,model=virtio \
        -net user,hostfwd=tcp::2222-:22 \
        -vga virtio \
        -display sdl,gl=on
else
	# Ubuntu image exists, so launch with CDROM option (user may be installing Ubuntu or just never removed the image)
	echo "Running with CDROM"
	echo "Once you've finished installing Ubuntu, it's safe to delete ubuntu-18.04.6-desktop-amd64.iso"
	qemu-system-x86_64 -enable-kvm -M q35 -smp 2 -m 4G \
        -hda illixr.qcow2 \
        -net nic,model=virtio \
        -net user,hostfwd=tcp::2222-:22 \
        -vga virtio \
        -cdrom ubuntu-22.04.2-desktop-amd64.iso \
        -display sdl,gl=on
fi
