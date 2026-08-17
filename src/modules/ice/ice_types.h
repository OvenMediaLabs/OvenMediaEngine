#pragma once

enum class IceConnectionState : int
{
	New,
	Checking,
	Connected,
	Completed,
	Failed,
	Disconnecting,
	Disconnected,
	Closed,
	Max,
};

static const char *IceConnectionStateToString(IceConnectionState state)
{
	switch (state)
	{
	case IceConnectionState::New:
		return "New";
	case IceConnectionState::Checking:
		return "Checking";
	case IceConnectionState::Connected:
		return "Connected";
	case IceConnectionState::Completed:
		return "Completed";
	case IceConnectionState::Failed:
		return "Failed";
	case IceConnectionState::Disconnecting:
		return "Disconnecting";
	case IceConnectionState::Disconnected:
		return "Disconnected";
	case IceConnectionState::Closed:
		return "Closed";
	case IceConnectionState::Max:
	default:
		return "Unknown";
	}
}