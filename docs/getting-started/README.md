# Getting Started

## Getting Started with Docker Image

OvenMediaEngine provides Docker images from AirenSoft's Docker Hub (airensoft/ovenmediaengine) repository. You can easily use OvenMediaEngine server by using Docker image. See [Getting Started with Docker](getting-started-with-docker.md) for details.

## Getting Started with Source Code

### Installing CMake

OvenMediaEngine supports most Linux distributions. The tested platforms are **Ubuntu 18+**, **Fedora 28+**, **Rocky Linux 8+**, and **AlmaLinux 8+**.

OvenMediaEngine requires **CMake 3.24 or later**. Check your installed version with:

```bash
cmake --version
```

{% hint style="warning" %}
The CMake version provided by some system package managers (e.g., `apt-get` on Ubuntu 22) may be older than 3.24. If your version does not meet the requirement, install a recent version from the [official CMake website](https://cmake.org/download/).
{% endhint %}

### **Building & Running**

You can build the OvenMediaEngine source using the following command:

{% tabs %}
{% tab title="Ubuntu 18" %}
```bash
sudo apt-get update
sudo apt-get install -y ninja-build pkg-config
cd OvenMediaEngine-master
cmake -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/Release
sudo cmake --install build/Release
systemctl start ovenmediaengine
# If you want automatically start on boot
systemctl enable ovenmediaengine.service 
```
{% endtab %}

{% tab title="Fedora 28" %}
```bash
sudo dnf update
sudo dnf install -y ninja-build pkg-config
cd OvenMediaEngine-master
cmake -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/Release
sudo cmake --install build/Release
systemctl start ovenmediaengine
# If you want automatically start on boot
systemctl enable ovenmediaengine.service
```
{% endtab %}

{% tab title="Rocky Linux 8" %}
```bash
sudo dnf update
sudo dnf config-manager --set-enabled codeready-builder-for-rhel-8-x86_64-rpms || sudo dnf config-manager --set-enabled powertools || true
sudo dnf install -y ninja-build
cd OvenMediaEngine-master
cmake -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/Release
sudo cmake --install build/Release
systemctl start ovenmediaengine
# If you want automatically start on boot
systemctl enable ovenmediaengine.service
```

{% endtab %}

{% tab title="AlmaLinux 8" %}
```bash
sudo dnf update
sudo dnf config-manager --set-enabled codeready-builder-for-rhel-8-x86_64-rpms || sudo dnf config-manager --set-enabled powertools || true
sudo dnf install -y ninja-build
cd OvenMediaEngine-master
cmake -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/Release
sudo cmake --install build/Release
systemctl start ovenmediaengine
# If you want automatically start on boot
systemctl enable ovenmediaengine.service
```

{% endtab %}
{% endtabs %}

{% hint style="info" %}
if `systemctl start ovenmediaengine` fails in Fedora, SELinux may be the cause. See [Check SELinux section of Troubleshooting](../troubleshooting.md#check-selinux).
{% endhint %}

## Ports used by default

The default configuration uses the following ports, so you need to open it in your firewall settings.

| Port                        | Purpose                                                                                                                                  |
| --------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| 1935/TCP                    | RTMP Input                                                                                                                               |
| 9999/UDP                    | SRT Input                                                                                                                                |
| 4000/UDP                    | MPEG-2 TS Input                                                                                                                          |
| 9000/TCP                    | Origin Server (OVT)                                                                                                                      |
| <p>3333/TCP<br>3334/TLS</p> | <p>LLHLS Streaming<br><mark style="color:red;"><strong>* Streaming over Non-TLS is not allowed with modern browsers.</strong></mark></p> |
| <p>3333/TCP<br>3334/TLS</p> | WebRTC Signaling (both ingest and streaming)                                                                                             |
| 3478/TCP                    | WebRTC TCP relay (TURN Server, both ingest and streaming)                                                                                |
| 10000 - 10009/UDP           | WebRTC Ice candidate (both ingest and streaming)                                                                                         |

{% hint style="warning" %}
To use TLS, you must set up a certificate. See [TLS Encryption](../configuration/tls-encryption.md) for more information.
{% endhint %}

You can open firewall ports as in the following example:

```bash
$ sudo firewall-cmd --add-port=3333/tcp
$ sudo firewall-cmd --add-port=3334/tcp
$ sudo firewall-cmd --add-port=1935/tcp
$ sudo firewall-cmd --add-port=9999/udp
$ sudo firewall-cmd --add-port=4000/udp
$ sudo firewall-cmd --add-port=3478/tcp
$ sudo firewall-cmd --add-port=9000/tcp
$ sudo firewall-cmd --add-port=10000-10009/udp
```
