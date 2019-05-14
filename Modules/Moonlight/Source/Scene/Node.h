#pragma once
#include <vector>

namespace Moonlight { class Mesh; }

namespace Moonlight
{
	class Node
	{
	public:
		Node() {}
		std::vector<Node> Nodes;
		std::vector<Moonlight::Mesh*> Meshes;
	};
}