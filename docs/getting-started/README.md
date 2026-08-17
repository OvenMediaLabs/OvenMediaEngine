---
title: Getting Started
description: "Get started with OvenMediaEngine using the official Docker image or the OME Docker Launcher."
sidebar_position: 4
---

import Tabs from '@theme/Tabs';
import TabItem from '@theme/TabItem';

## Getting Started with Docker Image

OvenMediaEngine provides Docker images from OvenMedia Labs Docker Hub (ovenmedialabs/ovenmediaengine) repository. You can easily use OvenMediaEngine server by using Docker image. See [Getting Started with Docker](getting-started-with-docker.md) for details.

## Getting Started with Source Code

### Installing CMake

OvenMediaEngine supports most Linux distributions. The tested platforms are **Ubuntu 18+**, **Fedora 28+**, **Rocky Linux 8+**, and **AlmaLinux 8+**.

OvenMediaEngine requires **CMake 3.24 or later**. Check your installed version with:

```bash
cmake --version
```


:::warning

The CMake version provided by some system package managers (e.g., `apt-get` on Ubuntu 22) may be older than 3.24. If your version does not meet the requirement, install a recent version from the [official CMake website](https://cmake.org/download/).

:::


### **Building & Running**

First, download and extract the source code:

```bash
curl -LOJ https://github.com/OvenMediaLabs/OvenMediaEngine/archive/master.tar.gz && \
tar xvfz OvenMediaEngine-master.tar.gz
```


:::tip[Building a specific release]

The command above builds the latest development code from the `master` branch. To build a **specific stable release** instead (recommended for production), choose a version from the [releases page](https://github.com/OvenMediaLabs/OvenMediaEngine/releases) and download its source archive:

```bash
# Set OME_VERSION to the release you want (see the releases page above)
OME_VERSION=0.20.5
curl -LOJ https://github.com/OvenMediaLabs/OvenMediaEngine/archive/v${OME_VERSION}.tar.gz && \
tar xvfz OvenMediaEngine-${OME_VERSION}.tar.gz
```

The build steps below are identical — just replace `OvenMediaEngine-master` with the extracted release directory (e.g. `OvenMediaEngine-0.20.5`).

:::


Then build and install using the commands for your platform:


<Tabs>
<TabItem value="ubuntu-18" label="Ubuntu 18">

```bash
sudo apt-get update
sudo apt-get install -y build-essential ninja-build pkg-config
cd OvenMediaEngine-master
cmake -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/Release
sudo cmake --install build/Release
sudo systemctl daemon-reload
sudo systemctl start ovenmediaengine
# If you want automatically start on boot
sudo systemctl enable ovenmediaengine.service
```

</TabItem>
<TabItem value="fedora-28" label="Fedora 28">

```bash
sudo dnf update
sudo dnf install -y ninja-build pkg-config
cd OvenMediaEngine-master
cmake -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/Release
sudo cmake --install build/Release
sudo systemctl daemon-reload
sudo systemctl start ovenmediaengine
# If you want automatically start on boot
sudo systemctl enable ovenmediaengine.service
```

</TabItem>
<TabItem value="rocky-linux-8" label="Rocky Linux 8">

```bash
sudo dnf update
sudo dnf install -y 'dnf-command(config-manager)'
sudo dnf config-manager --set-enabled crb || sudo dnf config-manager --set-enabled codeready-builder-for-rhel-8-x86_64-rpms || sudo dnf config-manager --set-enabled powertools || true
sudo dnf install -y ninja-build pkgconf-pkg-config
cd OvenMediaEngine-master
cmake -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/Release
sudo cmake --install build/Release
sudo systemctl daemon-reload
sudo systemctl start ovenmediaengine
# If you want automatically start on boot
sudo systemctl enable ovenmediaengine.service
```

</TabItem>
<TabItem value="almalinux-8" label="AlmaLinux 8">

```bash
sudo dnf update
sudo dnf install -y 'dnf-command(config-manager)'
sudo dnf config-manager --set-enabled crb || sudo dnf config-manager --set-enabled codeready-builder-for-rhel-8-x86_64-rpms || sudo dnf config-manager --set-enabled powertools || true
sudo dnf install -y ninja-build pkgconf-pkg-config
cd OvenMediaEngine-master
cmake -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/Release
sudo cmake --install build/Release
sudo systemctl daemon-reload
sudo systemctl start ovenmediaengine
# If you want automatically start on boot
sudo systemctl enable ovenmediaengine.service
```

</TabItem>
</Tabs>



:::info

if `systemctl start ovenmediaengine` fails in Fedora, SELinux may be the cause. See [Check SELinux section of Troubleshooting](../troubleshooting.md#check-selinux).

:::


## Ports used by default

The default configuration uses the following ports, so you need to open it in your firewall settings.

| Port                        | Purpose                                                                                                                                  |
| --------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| 1935/TCP                    | RTMP Input                                                                                                                               |
| 9999/UDP                    | SRT Input                                                                                                                                |
| 4000/UDP                    | MPEG-2 TS Input                                                                                                                          |
| 9000/TCP                    | Origin Server (OVT)                                                                                                                      |
| <p>3333/TCP<br />3334/TLS</p> | <p>LLHLS Streaming<br /><strong>* Streaming over Non-TLS is not allowed with modern browsers.</strong></p> |
| <p>3333/TCP<br />3334/TLS</p> | WebRTC Signaling (both ingest and streaming)                                                                                             |
| 3478/TCP                    | WebRTC TCP relay (TURN Server, both ingest and streaming)                                                                                |
| 10000/UDP, 10000/TCP        | WebRTC Ice candidate (both ingest and streaming)                                                                                         |


:::warning

To use TLS, you must set up a certificate. See [TLS Encryption](../configuration/tls-encryption.md) for more information.

:::


You can open firewall ports as in the following example:

```bash
$ sudo firewall-cmd --add-port=3333/tcp
$ sudo firewall-cmd --add-port=3334/tcp
$ sudo firewall-cmd --add-port=1935/tcp
$ sudo firewall-cmd --add-port=9999/udp
$ sudo firewall-cmd --add-port=4000/udp
$ sudo firewall-cmd --add-port=3478/tcp
$ sudo firewall-cmd --add-port=9000/tcp
$ sudo firewall-cmd --add-port=10000-10003/udp
$ sudo firewall-cmd --add-port=10000/tcp
```
