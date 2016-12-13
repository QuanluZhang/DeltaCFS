#!/bin/bash
rm -r /tmp/gearmanserver
rm -r /tmp/gearmandb
mkdir -p /tmp/gearmanserver
mkdir -p /tmp/gearmandb
./deltacfs_server_gearman
