#!/bin/sh
set -x

if [ -n "$1" ];then
    ip tuntap add $1 mode tap
    ip addr add 172.16.0.1/24 dev $1
    ip link set $1 up
    sleep 0.5s
    iptables-save > iptables-save
    iptables -t nat -A POSTROUTING -o ens8f0 -j MASQUERADE
    iptables -A FORWARD -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT
    iptables -A FORWARD -i tap0 -o ens8f0 -j ACCEPT
    iptables -A FORWARD -i ens8f0 -j ACCEPT
    iptables -t nat -A PREROUTING -i ens8f0 -p tcp --dport 8080 -j DNAT --to-destination 172.16.0.2
    exit 0
else
    echo "Error: no interface specified"
    exit 1
fi

# add above content to /home/kingdo/CLionProjects/qemu/etc/qemu-ifup
