#!/bin/bash
rm -r /tmp/cloudsyncclient
rm -r /tmp/cloudsynctmp
rm -r /tmp/cloudsyncfuse
rm -r /tmp/cloudsyncclientdb
mkdir -p /tmp/cloudsyncclient
mkdir -p /tmp/cloudsynctmp
mkdir -p /tmp/cloudsyncfuse
mkdir -p /tmp/cloudsyncclientdb
./deltacfs_client_gearman -o modules=subdir,subdir=/tmp/cloudsyncclient,big_writes /tmp/cloudsyncfuse -f
umount /tmp/cloudsyncfuse
