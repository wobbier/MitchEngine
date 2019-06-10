#pragma once
#include <vector>
#include <map>
#include <string>
#include "Resource.h"
#include "Path.h"
#include "Singleton.h"
#include "ClassTypeId.h"
#include <memory>

class Resource;

class ResourceCache
{
	ResourceCache();
	~ResourceCache();

public:
	void Push();
	void Pop();


	size_t GetStackSize();

	template<class T, typename... Args>
	std::shared_ptr<T> Get(const Path& InFilePath, Args&& ... args);

	void TryToDestroy(Resource* resource);

private:

	std::vector<std::map<std::string, std::shared_ptr<Resource>>> ResourceStack;
	ME_SINGLETON_DEFINITION(ResourceCache)
};

template<class T, typename... Args>
std::shared_ptr<T> ResourceCache::Get(const Path& InFilePath, Args&& ... args)
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
	std::shared_ptr<T> Res = std::make_shared<T>(InFilePath, std::forward<Args>(args)...);
	Res->Resources = this;
	ResourceStack[ResourceStack.size() - 1][InFilePath.FullPath] = Res;
	return Res;
}
