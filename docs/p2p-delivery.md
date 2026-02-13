# P2P Delivery (Experiment)

OvenMediaEngine provides P2P Delivery to be able to distribute Edge Traffic to Player. This feature is currently the Preview version, and if you want to use it, you need only to use OvenPlayer. Moreover, we plan to perform more experiments in various real-world and then upgrade it to the full version in OvenMediaEngine.

First of all, we have rules. The peer that sends the Traffic in the P2P network is called a Host Peer, and the peer that receives the Traffic from the Host Peer is called a Client Peer. Also, P2P Delivery in OvenMediaEngine doesn't designate the Client Peer as the Host Peer again. In other words, it only operates as 1 Depth.

## What are the benefits of using P2P Delivery?

According to our experiments so far, P2P Delivery provides the best performance and stability when using 1 Depth to connect between Players and connecting up to two Players to one Player.

In other words, P2P Delivery has distributed two-thirds of existing Traffic. So, this means that it can expand the Capacity of the Edge Network by three times and reduce Traffic costs by two-thirds.

![](<.gitbook/assets/image (9).png>)

## How does it work?

You can use the P2P function of OvenMediaEngine by adding the `<P2P>` element as the following settings:

{% code title="Server.xml" %}
```markup
<Server version="...">
	...
	<P2P>
		<MaxClientPeersPerHostPeer>2</MaxClientPeersPerHostPeer>
	</P2P>
	...
</Server>
```
{% endcode %}

Also, If you want to use P2P Delivery when your OvenMediaEngine is running in Origin-Edge Cluster-Mode, you need to apply this setting to all the Edges. You can instantly test P2P Delivery with OvenPlayer.

* `<MaxClientPeersPerHostPeer>` sets the number of Client Peers connecting to one Host Peer.

## How does it classify Peers?

When OvenMediaEngine receives a WebRTC connection request from a new player, it determines the Host Peer or Client Peer according to the following rules:

| Qualification for Host Peer                                                                                                                  | Qualification for Client Peer                                                                     |
| -------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------- |
| <ul><li>The device isn't Mobile</li><li>OS isn't Linux</li><li>Browser isn't MS Edge Browser</li><li>Browser isn't Unknown Browser</li></ul> | <ul><li>One of the Host Peers uses the same kind of browser</li><li>Host Peer is vacant</li></ul> |

{% hint style="info" %}
When any Host Peer is disconnected, OvenMediaEngine detects this situation and immediately reconnects the Client Peer connected to that Host Peer to the Edge to ensure stability.

Also, we are preparing a smarter algorithm based on user location, platform performance, and network statistical information for classifying Host Peers or Client Peers.
{% endhint %}

If you have a better idea, we hope that you improve our code and contribute to our project. Please visit [OvenMediaEngine GitHub](https://github.com/AirenSoft/OvenMediaEngine).

{% embed url="https://github.com/AirenSoft/OvenMediaEngine/blob/master/src/projects/modules/rtc_signalling/p2p" %}
