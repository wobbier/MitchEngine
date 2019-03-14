#pragma once
#include <vector>
#include <map>
#include <string>
#include "Resource.h"
#include "FilePath.h"
#include "Singleton.h"
#include "ClassTypeId.h"
#include <memory>

class Resource;

class ResourceCache
	: public Singleton<ResourceCache>
{
	friend class Singleton<ResourceCache>;
	ResourceCache();
	~ResourceCache();

public:
	void Push();
	void Pop();


	size_t GetStackSize();

	template<class T>
	std::shared_ptr<T> Get(const FilePath& InFilePath);

	void TryToDestroy(Resource* resource);

private:

	std::vector<std::map<std::string, std::shared_ptr<Resource>>> ResourceStack;
};

template<class T>
std::shared_ptr<T> ResourceCache::Get(const FilePath& InFilePath)
{
	TypeId id = ClassTypeId<Resource>::GetTypeId<T>();
	std::map<std::string, std::shared_ptr<Resource>>::iterator I;
	for (int i = static_cast<int>(ResourceStack.size() - 1); i >= 0; i--)
	{
		I = ResourceStack[i].find(InFilePath.FullPath);
		if (I != ResourceStack[i].end())
		{
			std::shared_ptr<T> Res = std::dynamic_pointer_cast<T>(I->second);
			return Res;
		}
	}
	std::shared_ptr<T> Res = std::make_shared<T>(InFilePath);
	Res->Resources = this;
	ResourceStack[ResourceStack.size() - 1][InFilePath.FullPath] = Res;
	return Res;
}
