# Getting Started with Docker

## Getting Started with default settings

OvenMediaEngine provides the Docker image from [AirenSoft's Docker Hub](https://hub.docker.com/r/airensoft/ovenmediaengine) (**airensoft/ovenmediaengine)** repository. After installing [Docker](https://www.docker.com), you can simply run the following command:

```sh
docker run --name ome -d -e OME_HOST_IP=Your.HOST.IP.Address \
-p 1935:1935 -p 9999:9999/udp -p 9000:9000 -p 3333:3333 -p 3478:3478 -p 10000-10009:10000-10009/udp \
airensoft/ovenmediaengine:latest
```

{% hint style="warning" %}
If a certificate is not installed in OvenMediaEngine, some functions (WebRTC Ingest, LLHLS playback) may not work due to the browser's security policy. Please refer to [Complex Configuration](getting-started-with-docker.md#getting-started-with-complex-configuration) section to install the certificate.
{% endhint %}

You can set the following environment variables.

### Environment Variables

| Env                              | Default Value     |
| -------------------------------- | ----------------- |
| `OME_HOST_IP`                    | `*`               |
| `OME_ORIGIN_PORT`                | `9000`            |
| `OME_RTMP_PROV_PORT`             | `1935`            |
| `OME_SRT_PROV_PORT`              | `9999/udp`        |
| `OME_MPEGTS_PROV_PORT`           | `4000/udp`        |
| `OME_LLHLS_STREAM_PORT`          | `3333`            |
| `OME_LLHLS_STREAM_TLS_PORT`      | `3334`            |
| `OME_WEBRTC_SIGNALLING_PORT`     | `3333`            |
| `OME_WEBRTC_SIGNALLING_TLS_PORT` | `3334`            |
| `OME_WEBRTC_TCP_RELAY_PORT`      | `3478`            |
| `OME_WEBRTC_CANDIDATE_PORT`      | `10000-10004/udp` |

## Getting Started with Complex Configuration

When you need to install a certificate in OME or apply a complex configuration, you can do it by following the procedure below to modify Server.xml inside Docker.

OvenMediaEngine docker container loads configuration files from the following path.

| Type               | Path / Description                                                                                                                                                 |
| ------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `Server.xml`       | `/opt/ovenmediaengine/bin/origin_conf/Server.xml`                                                                                                                  |
| `Logger.xml`       | `/opt/ovenmediaengine/bin/origin_conf/Logger.xml`                                                                                                                  |
| Server Certificate | <p><code>/opt/ovenmediaengine/bin/origin_conf/cert.crt</code><br><br>Server certificate file in PEM format. The intermediate certificate must not be included.</p> |
| Private Key        | <p><code>/opt/ovenmediaengine/bin/origin_conf/cert.key</code><br><br>This is the private key file of the certificate.</p>                                          |
| CA Bundle          | <p><code>/opt/ovenmediaengine/bin/origin_conf/cert.ca-bundle</code><br><br>A file containing root and intermediate certificates.</p>                               |

There are many ways to change files inside a Docker container, but this document describes how to change them using Docker's bind mounts.

### Setup

#### Create the directories

{% code overflow="wrap" %}
```sh
export OME_DOCKER_HOME=/opt/ovenmediaengine
sudo mkdir -p $OME_DOCKER_HOME/conf
sudo mkdir -p $OME_DOCKER_HOME/logs

# Set permissions for the created directory if necessary.
sudo chgrp -R docker $OME_DOCKER_HOME 
sudo chmod -R 775 $OME_DOCKER_HOME

# If you want to use OME_HOME permanently, add the following line to the ~/.profile file for bash, for other shells, you can do it accordingly.
echo "export OME_DOCKER_HOME=/opt/ovenmediaengine" >> ~/.profile
```
{% endcode %}

#### Copy the default configurations from Docker container

```sh
docker run -d --name tmp-ome airensoft/ovenmediaengine:latest
docker cp tmp-ome:/opt/ovenmediaengine/bin/origin_conf/Server.xml $OME_DOCKER_HOME/conf
docker cp tmp-ome:/opt/ovenmediaengine/bin/origin_conf/Logger.xml $OME_DOCKER_HOME/conf
docker rm -f tmp-ome
```

#### Copy the certificate files to the directory

Copy your PEM certificate files to the path below if you need to enable TLS. The destination file names must match if using the default configuration. If you want to change the file name, you can do so by editing the Server.xml configuration file. See [TLS Encryption](../configuration/tls-encryption.md) for details.

```sh
cp /your/server_certificate_file.crt $OME_DOCKER_HOME/conf/cert.crt
cp /your/certificate_key_file.key $OME_DOCKER_HOME/conf/cert.key
cp /your/ca_bundle_file.ca-bundle $OME_DOCKER_HOME/conf/cert.ca-bundle
```

#### Modify Server.xml if necessary

```sh
vi $OME_DOCKER_HOME/conf/Server.xml
```

### Running

#### Run Docker Container

The command below will make your OvenMediaEngine docker container run with $OME\_DOCKER\_HOME/conf/Server.xml and $OME\_DOCKER\_HOME/conf/Logger.xml files on your host. It will also create $OME\_DOCKER\_HOME/logs/ovenmediaengine.log file.

{% code overflow="wrap" %}
```sh
docker run -d -it --name ome -e OME_HOST_IP=Your.HOST.IP.Address \
-v $OME_DOCKER_HOME/conf:/opt/ovenmediaengine/bin/origin_conf \
-v $OME_DOCKER_HOME/logs:/var/log/ovenmediaengine \
-p 1935:1935 -p 9999:9999/udp -p 9000:9000 -p 3333:3333 -p 3478:3478 \
-p 10000-10009:10000-10009/udp \
airensoft/ovenmediaengine:latest
```
{% endcode %}

#### Check the log file

```sh
tail -f $OME_DOCKER_HOME/logs/ovenmediaengine.log
```

#### Restart Docker Container

```sh
docker restart ome
```

#### Stop and Remove Container

```sh
docker stop ome
docker rm ome
```
