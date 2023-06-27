#!/bin/bash
script_directory=$(dirname "$0")
firecracker_cmd=/home/kingdo/CLionProjects/firecracker/build/cargo_target/x86_64-unknown-linux-musl/release/firecracker

sudo setfacl -m u:${USER}:rw /dev/kvm
sudo rm -f /tmp/firecracker.socket
sudo $script_directory/../network/ifup.sh tap0
$firecracker_cmd --api-sock /tmp/firecracker.socket
sudo $script_directory/../network/ifdown.sh tap0
