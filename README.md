# DeltaCFS

This is a cloud data synchronization prototype system for both desktop and mobiles, which could synchronize file changes on the client side onto the cloud in a timely manner. DeltaCFS optimizes both computation and network overhead on both client and server sides, makes it very appealing to the increasing demand of timely synchronization.


In the trace/ directory, we provide two typical traces on the desktop/mobile environment.

##Dependencies

Debian/Ubuntu:
```apt-get install build-essential libgearman-dev librsync-dev gearman-server libfuse-dev libleveldb-dev libhiredis-dev redis-server pkg-config uthash-dev```

CentOS:
```yum install fuse-devel librsync-devel libgearman-devel gearmand leveldb-devel uthash-devel hiredis-devel redis gcc gcc-c++```

##Build and run

Client:

- in librsync-mod, ```./configure``` and ```make```

- in client, ```make``` and ```sudo run_client.sh```

Server:

- in server, ```make``` and ```run_server.sh```
