# Getting Started with OME Docker Launcher

## Overview and Features

The OME Docker Launcher is a tool that simplifies the process of deploying and managing the OvenMediaEngine (OME) application using Docker containers. This tool can be used by developers and system administrators who want to quickly deploy and test the OME application in a Docker environment.

<figure><img src="../.gitbook/assets/image (1) (1) (1) (1) (1) (1) (1).png" alt=""><figcaption></figcaption></figure>

The OME Docker Launcher provides a set of commands that allow users to easily manage the OME Docker container. These commands include:

* [`setup`](getting-started-with-ome-docker-launcher.md#setup)
  * This command pulls the OME Docker image(`airensoft/ovenmediaengine:latest`) from the Docker registry and copies the necessary configuration files to a specified location. This command needs to be run before starting the OME Docker container.
* [`start`](getting-started-with-ome-docker-launcher.md#start)
  * This command creates and starts the Docker container. Once the container is started, the OME application can be accessed through a web browser using the container's IP address.
* [`sh`](getting-started-with-ome-docker-launcher.md#sh)
  * This command launches a bash shell inside the running OME Docker container, allowing users to execute commands and interact with the container.
* [`status`](getting-started-with-ome-docker-launcher.md#status)
  * This command displays the status of the running OME Docker container, including information such as the container name, and running status.
* [`stop`](getting-started-with-ome-docker-launcher.md#stop)
  * This command stops the running OME Docker container.
* [`restart`](getting-started-with-ome-docker-launcher.md#restart)
  * This command stops and then starts the OME Docker container.

Using the OME Docker Launcher, you can easily set up and manage an OME Docker container, without having to manually configure and manage the Docker container. This can save time and effort, especially for users who are not familiar with Docker or who do not want to spend time manually setting up and configuring the OME application.

{% hint style="info" %}
OME Docker Launcher has not been tested in various environments yet. Therefore, sharing any issues that occur while using it is always welcome.
{% endhint %}

## Installation

Run the following command in your Linux shell.

```
curl -OL 'https://raw.githubusercontent.com/AirenSoft/OvenMediaEngine/master/misc/ome_docker_launcher.sh' && chmod +x ome_docker_launcher.sh
```

Below is an example of execution:

```bash
$ curl -OL 'https://raw.githubusercontent.com/AirenSoft/OvenMediaEngine/master/misc/ome_docker_launcher.sh' && chmod +x ome_docker_launcher.sh
$ ./ome_docker_launcher.sh -h

 ▄██████▀███▄
█████▀ ▄██████  OvenMediaEngine Launcher v0.1
███▄▄▄▄▀▀▀▀███
██████▀ ▄█████  https://github.com/AirenSoft/OvenMediaEngine
 ▀███▄██████▀

• Usage: ./ome_docker_launcher.sh [OPTIONS] COMMAND ...

• Options:
  -h, --help                        Show this help message and exit
  -v, --version                     Show the version and exit
  -d, --debug                       Show debug log
  -b, --hide_banner                 Hide the banner
  -m, --monochrome                  Disable colors

• Commands:
  setup       Download the latest Docker image and setup directories for the container
  start       Start a docker container
  sh          Run a shell in the docker container
  status      Show the status of the docker container
  stop        Stop the docker container
  restart     Restart the docker container
```

## Commands

OME Docker Launcher can be executed in the following format:

```bash
• Usage: ./ome_docker_launcher.sh [OPTIONS] COMMAND ...

• Options:
  -h, --help                        Show this help message and exit
  -v, --version                     Show the version and exit
  -d, --debug                       Show debug log
  -b, --hide_banner                 Hide the banner
  -m, --monochrome                  Disable colors

• Commands:
  setup       Download the latest Docker image and setup directories for the container
  start       Start a docker container
  sh          Run a shell in the docker container
  status      Show the status of the docker container
  stop        Stop the docker container
  restart     Restart the docker container
```

### `setup`

The `setup` command pulls the OME Docker image from the Docker registry and copies the necessary configuration files to the host's `/usr/share/ovenmediaengine` directory. Additionally, it initializes the log path and crash dump path that will be mounted into the container when it is run.

This command prepares the host environment for running the OME Docker container and sets up the necessary directories and configurations for the container to run correctly.

If you run the "setup" command, the following files and directories will be created:

* `/usr/share/ovenmediaengine/conf`
  * This directory contains the OME configuration files and is mounted into the container when it is run.
* `/usr/share/ovenmediaengine/logs`
  * This directory is the log path for OME and is mounted into the container when it is run. Log files generated by OME will be stored in this directory.
* `/usr/share/ovenmediaengine/dumps`
  * This directory is the crash dump path for OME and is mounted into the container when it is run. Crash dumps generated by OME will be stored in this directory.

```bash
$ ./ome_docker_launcher.sh setup

 ▄██████▀███▄ 
█████▀ ▄██████  OvenMediaEngine Launcher v0.1
███▄▄▄▄▀▀▀▀███
██████▀ ▄█████  https://github.com/AirenSoft/OvenMediaEngine
 ▀███▄██████▀ 

• Creating configuration directory /usr/share/ovenmediaengine/conf
• Copying configuration to /usr/share/ovenmediaengine/conf
• Copying logs directory
• Copying crash dump directory
• OvenMediaEngine is ready to start!

If you want to change the settings, please modify /usr/share/ovenmediaengine/conf/Server.xml
If you want to start OvenMediaEngine, please run ./ome_docker_launcher.sh start
```

If you want to change the configuration of OME, you can edit the `/usr/share/ovenmediaengine/conf/Server.xml` file. This file contains the server configuration settings for OME, such as the server's IP address, port, and SSL settings. Once you have made changes to this file, you will need to restart the OME Docker container for the changes to take effect. You can do this by running the `restart` command provided by the OME Docker Launcher.

#### Certificate Installation

To install a certificate in OvenMediaEngine, copy the certificate files to /usr/share/ovenmediaengine/conf with the following names:

<table><thead><tr><th width="219">Type</th><th>File name</th></tr></thead><tbody><tr><td>Certificate</td><td>cert.crt</td></tr><tr><td>Private Key</td><td>cert.key</td></tr><tr><td>CA Bundle</td><td>cert.ca-bundle</td></tr></tbody></table>

If you want to change the file names, you can modify Server.xml.

### `start`

Once the `setup` phase is complete, you can use the `start` command to run the OME Docker container. The `start` command creates and starts the Docker container, enabling the OME application to receive stream packets using protocols such as RTMP and SRT. Before running the `start` command, ensure that the necessary configuration files have been copied to the host's `/usr/share/ovenmediaengine` directory by running the `setup` command.

```bash
$ ./ome_docker_launcher.sh start

 ▄██████▀███▄ 
█████▀ ▄██████  OvenMediaEngine Launcher v0.1
███▄▄▄▄▀▀▀▀███
██████▀ ▄█████  https://github.com/AirenSoft/OvenMediaEngine
 ▀███▄██████▀ 

• Starting OvenMediaEngine...
• Obtaining the port list from /usr/share/ovenmediaengine/conf/Server.xml
  - RTMP Provider is configured to use 1935 (Port)
  - SRT Provider is configured to use 9999 (Port)
  - WebRTC Provider is configured to use 3333 (Port)
  - WebRTC Provider is configured to use 3334 (TLSPort)
  - WebRTC Provider is configured to use 10000-10004/UDP (IceCandidate)
  - WebRTC Provider is configured to use 3478 (TcpRelay)
  - OVT Publisher is configured to use 9000 (Port)
  - LLHLS Publisher is configured to use 3333 (Port)
  - LLHLS Publisher is configured to use 3334 (TLSPort)
  - WebRTC Publisher is configured to use 3333 (Port)
  - WebRTC Publisher is configured to use 3334 (TLSPort)
  - WebRTC Publisher is configured to use 10000-10004/UDP (IceCandidate)
  - WebRTC Publisher is configured to use 3478 (TcpRelay)
• Starting a container: ovenemediaengine
  docker> 7235ff9f80762b6e7b27ba3a9773f5584033d55c113340dabf0779e8f5cf53bb
• OvenMediaEngine is started successfully!
```

{% hint style="info" %}
When running the OME Docker Launcher, you can specify the IP to be used as an ICE Candidate by using the `OME_HOST_IP` environment variable. For instance, specifying the `OME_HOST_IP` as shown below will propagate the ICE Candidate to that particular address.



```sh
$ OME_HOST_IP=1.2.3.4 ./ome_docker_launcher.sh start
...
• OvenMediaEngine is started successfully!

$ tail -f /usr/share/ovenmediaengine/logs/ovenmediaengine.log
...
[2023-11-01 00:00:00.000] I [OvenMediaEngine:1] ICE | ice_port_manager.cpp:305  | ICE candidate found: 1.2.3.4:40000
...
```
{% endhint %}

{% hint style="info" %}
The OME Docker Launcher automatically detects a list of port numbers specified in the `Server.xml` file and passes them to the Docker `-p` option. However, if you use the `include` attribute inside the `<Providers>` or `<Publishers>` element, the launcher may not detect them correctly.
{% endhint %}

{% hint style="info" %}
If you have declared the following environment variable in the shell where you run the OME Docker Launcher, this value will be used to bind the port and passed into the Docker container. This enables you to dynamically set configuration values using environment variables.

```
OME_HOST_IP
OME_RTMP_PROV_PORT
OME_WEBRTC_CANDIDATE_IP
OME_WEBRTC_CANDIDATE_PORT
OME_WEBRTC_SIGNALLING_PORT
OME_WEBRTC_SIGNALLING_TLS_PORT
OME_WEBRTC_TCP_RELAY_PORT
```
{% endhint %}

### `sh`

The `sh` command allows you to enter into the shell of the running container. You can use this command for troubleshooting purpose. Once you enter into the container's shell, you can execute any commands just like you do in a normal Linux shell. This allows you to inspect the container's internal state and debug any issues that you might be facing with the container or the application running inside it.

```bash
$ ./ome_docker_launcher.sh sh

 ▄██████▀███▄ 
█████▀ ▄██████  OvenMediaEngine Launcher v0.1
███▄▄▄▄▀▀▀▀███
██████▀ ▄█████  https://github.com/AirenSoft/OvenMediaEngine
 ▀███▄██████▀ 

• Run a shell in the running container: ID: 7235ff9f8076
root@7235ff9f8076:/opt/ovenmediaengine/bin# ps -ef
UID          PID    PPID  C STIME TTY          TIME CMD
root           1       0  0 10:29 ?        00:00:01 /opt/ovenmediaengine/bin/OvenMediaEngine -c origin_conf
root          53       0  0 10:44 pts/0    00:00:00 /bin/bash
root          61      53  0 10:44 pts/0    00:00:00 ps -ef
root@7235ff9f8076:/opt/ovenmediaengine/bin# top -bn1
top - 10:44:44 up 333 days,  3:33,  0 users,  load average: 0.44, 0.78, 0.78
Tasks:   3 total,   1 running,   2 sleeping,   0 stopped,   0 zombie
%Cpu(s):  1.4 us,  0.3 sy,  0.0 ni, 98.3 id,  0.0 wa,  0.0 hi,  0.0 si,  0.0 st
MiB Mem : 128723.7 total,  10529.4 free,  31268.5 used,  86925.7 buff/cache
MiB Swap:  31250.0 total,  30345.8 free,    904.2 used.  96221.5 avail Mem 

    PID USER      PR  NI    VIRT    RES    SHR S  %CPU  %MEM     TIME+ COMMAND
      1 root      20   0  320136  21812  15772 S   0.0   0.0   0:01.48 OvenMediaEngine
     53 root      20   0    4116   3456   2896 S   0.0   0.0   0:00.01 bash
     62 root      20   0    5972   3160   2732 R   0.0   0.0   0:00.00 top
root@7235ff9f8076:/opt/ovenmediaengine/bin# 
```

### `status`

The `status` command shows the current execution status of the container. If the container is running, it displays the ID and name of the container. This command helps you to verify whether the container is up and running or not. If the container is not running, you can use the `start` command to start the container.

```bash
$ ./ome_docker_launcher.sh status

 ▄██████▀███▄ 
█████▀ ▄██████  OvenMediaEngine Launcher v0.1
███▄▄▄▄▀▀▀▀███
██████▀ ▄█████  https://github.com/AirenSoft/OvenMediaEngine
 ▀███▄██████▀ 

• Container is running: ID: 7235ff9f8076, name: ovenemediaengine
```



### `stop`

The `stop` command stops the running container and removes it from the list of Docker containers.&#x20;

```bash
$ ./ome_docker_launcher.sh stop

 ▄██████▀███▄ 
█████▀ ▄██████  OvenMediaEngine Launcher v0.1
███▄▄▄▄▀▀▀▀███
██████▀ ▄█████  https://github.com/AirenSoft/OvenMediaEngine
 ▀███▄██████▀ 

• Stopping a container: ovenemediaengine
  docker> ovenemediaengine
• Removing a container: ovenemediaengine
  docker> ovenemediaengine
• OvenMediaEngine is stopped successfully
```

### `restart`

The `restart` command restarts the container. This is useful when you need to apply changes to the `Server.xml`.

```bash
$ ./ome_docker_launcher.sh stop

 ▄██████▀███▄ 
█████▀ ▄██████  OvenMediaEngine Launcher v0.1
███▄▄▄▄▀▀▀▀███
██████▀ ▄█████  https://github.com/AirenSoft/OvenMediaEngine
 ▀███▄██████▀ 

• Restarting a container: ovenemediaengine
  docker> ovenemediaengine
```

## Troubleshootings

### Enable debug log

If you encounter any problems during the execution, try using the `-d` option in the `[OPTIONS]` to view detailed logs. This option shows the command sets and their results that are executed internally.

```bash
$ ./ome_docker_launcher.sh -d stop

 ▄██████▀███▄ 
█████▀ ▄██████  OvenMediaEngine Launcher v0.1
███▄▄▄▄▀▀▀▀███
██████▀ ▄█████  https://github.com/AirenSoft/OvenMediaEngine
 ▀███▄██████▀ 

• Stopping a container: ovenemediaengine
 ┌── /usr/bin/docker stop ovenemediaengine
   docker> ovenemediaengine
 └── Succeeded
• Removing a container: ovenemediaengine
 ┌── /usr/bin/docker rm ovenemediaengine
   docker> ovenemediaengine
 └── Succeeded
• OvenMediaEngine is stopped successfully
```

### Get the crash dump of OME in the container

If OME terminates abnormally, providing the crash dump to the OME team can be helpful. The crash dump is stored in the `/usr/share/ovenmediaengine/dumps` directory, which is created during the `setup` phase. You can find the dump file named `crash_<yyyymmdd>.dump` in this directory.

Sharing those log and dump file would be greatly appreciated and helpful for the development of OME.
