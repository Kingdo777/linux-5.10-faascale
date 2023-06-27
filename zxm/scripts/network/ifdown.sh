#!/bin/sh
set -x

if [ -n "$1" ];then
    ip link del tap0
    sleep 0.5s
    iptables-restore iptables-save
    exit 0
else
    echo "Error: no interface specified"
    exit 1
fi

# add above content to /home/kingdo/CLionProjects/qemu_6_1_0/etc/qemu-ifdown
