//==============================================================================
//
//  OvenMediaEngine
//
//  Created by getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "node.h"

namespace ov
{
	Node::Node(NodeType node_type)
	{
		_node_type = node_type;
	}

	Node::~Node()
	{
		
	}

	NodeType Node::GetNodeType()
	{
		return _node_type;
	}

	bool Node::Start()
	{
		_node_state = NodeState::Started;
		return true;
	}

	bool Node::Stop()
	{
		_node_state = NodeState::Stopped;

		_next_nodes.clear();
		_prev_nodes.clear();

		return true;
	}

	Node::NodeState Node::GetNodeState()
	{
		return _node_state;
	}

	std::shared_ptr<Node> Node::GetNextNode()
	{
		if (_next_nodes.empty())
		{
			return nullptr;
		}

		return _next_nodes.begin()->second;
	}

	std::shared_ptr<Node> Node::GetPrevNode()
	{
		if (_prev_nodes.empty())
		{
			return nullptr;
		}

		return _prev_nodes.begin()->second;
	}

	std::shared_ptr<Node> Node::GetNextNode(NodeType node_type)
	{
		if (_next_nodes.count(node_type) == 0)
		{
			return nullptr;
		}

		return _next_nodes[node_type];
	}

	std::shared_ptr<Node> Node::GetPrevNode(NodeType node_type)
	{
		if (_prev_nodes.count(node_type) == 0)
		{
			return nullptr;
		}

		return _prev_nodes[node_type];
	}

	void Node::RegisterPrevNode(const std::shared_ptr<Node> &node)
	{
		if (!node)
		{
			return;
		}

		_prev_nodes[node->GetNodeType()] = node;
	}

	void Node::RegisterNextNode(const std::shared_ptr<Node> &node)
	{
		if (!node)
		{
			return;
		}

		_next_nodes[node->GetNodeType()] = node;
	}

	bool Node::SendDataToPrevNode(const std::shared_ptr<const ov::Data> &data)
	{
		return SendDataToPrevNode(GetNodeType(), data);
	}

	bool Node::SendDataToNextNode(const std::shared_ptr<ov::Data> &data)
	{
		return SendDataToNextNode(GetNodeType(), data);	
	}

	bool Node::SendDataToPrevNode(NodeType node_type, const std::shared_ptr<const ov::Data> &data)
	{
		auto node = GetPrevNode();
		if(node == nullptr)
		{
			return false;
		}

		return node->OnDataReceivedFromNextNode(node_type, data);
	}

	bool Node::SendDataToNextNode(NodeType node_type, const std::shared_ptr<ov::Data> &data)
	{
		auto node = GetNextNode();
		if(node == nullptr)
		{
			return false;
		}

		return node->OnDataReceivedFromPrevNode(node_type, data);
	}
}  // namespace pub