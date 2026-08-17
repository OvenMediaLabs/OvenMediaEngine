//==============================================================================
//
//  OvenMediaEngine
//
//  Created by getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include <base/common_types.h>
#include <base/ovlibrary/ovlibrary.h>

typedef uint32_t node_id_t;

namespace ov
{
	class Node : public ov::EnableSharedFromThis<Node>
	{
	public:
		Node(NodeType node_type);
		virtual ~Node();

		NodeType GetNodeType();

		virtual void RegisterPrevNode(const std::shared_ptr<Node> &node);
		virtual void RegisterNextNode(const std::shared_ptr<Node> &node);

		virtual bool Start();
		virtual bool Stop();

		enum class NodeState : int8_t
		{
			Ready,
			Started,
			Stopped,
			Error
		};

		NodeState GetNodeState();

		virtual bool OnDataReceivedFromPrevNode(NodeType from_node, const std::shared_ptr<ov::Data> &data) = 0;
		virtual bool OnDataReceivedFromNextNode(NodeType from_node, const std::shared_ptr<const ov::Data> &data) = 0;

	protected:
		bool SendDataToPrevNode(NodeType node_type, const std::shared_ptr<const ov::Data> &data);
		bool SendDataToNextNode(NodeType node_type, const std::shared_ptr<ov::Data> &data);

		bool SendDataToPrevNode(const std::shared_ptr<const ov::Data> &data);
		bool SendDataToNextNode(const std::shared_ptr<ov::Data> &data);

		std::shared_ptr<Node> GetPrevNode();
		std::shared_ptr<Node> GetNextNode();

		std::shared_ptr<Node> GetPrevNode(NodeType node_type);
		std::shared_ptr<Node> GetNextNode(NodeType node_type);

		std::map<NodeType, std::shared_ptr<Node>> _prev_nodes;
		std::map<NodeType, std::shared_ptr<Node>> _next_nodes;

	private:
		NodeType _node_type = NodeType::Unknown;
		NodeState _node_state = NodeState::Ready;
	};
}  // namespace ov