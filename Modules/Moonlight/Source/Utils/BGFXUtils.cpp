#include "BGFXUtils.h"

#include "Path.h"
#include "Graphics/ShaderFile.h"
#include "Resource/ResourceCache.h"
#include "Pointers.h"

const bgfx::Memory* Moonlight::LoadMemory(bx::FileReaderI* _reader, const char* _filePath)
{
	if (bx::open(_reader, _filePath))
	{
		uint32_t size = (uint32_t)bx::getSize(_reader);
		const bgfx::Memory* mem = bgfx::alloc(size + 1);
		bx::read(_reader, mem->data, size);
		bx::close(_reader);
		mem->data[mem->size - 1] = '\0';
		return mem;
	}

	YIKES(std::string("Failed to load: ") + std::string( _filePath));
	return NULL;
}

bgfx::ShaderHandle Moonlight::LoadShader(const char* _name)
{
	char filePath[512];

	const char* shaderPath = "???";

	switch (bgfx::getRendererType())
	{
	case bgfx::RendererType::Noop:
	case bgfx::RendererType::Direct3D9:  shaderPath = "Assets/Shaders/dx9/";   break;
	case bgfx::RendererType::Direct3D11:
	case bgfx::RendererType::Direct3D12: shaderPath = "Assets/Shaders/dx11/";  break;
	case bgfx::RendererType::Gnm:        shaderPath = "Assets/Shaders/pssl/";  break;
	case bgfx::RendererType::Metal:      shaderPath = "Assets/Shaders/metal/"; break;
	case bgfx::RendererType::Nvn:        shaderPath = "Assets/Shaders/nvn/";   break;
	case bgfx::RendererType::OpenGL:     shaderPath = "Assets/Shaders/glsl/";  break;
	case bgfx::RendererType::OpenGLES:   shaderPath = "Assets/Shaders/essl/";  break;
	case bgfx::RendererType::Vulkan:     shaderPath = "Assets/Shaders/spirv/"; break;
	case bgfx::RendererType::WebGPU:     shaderPath = "Assets/Shaders/spirv/"; break;

	case bgfx::RendererType::Count:
		BX_ASSERT(false, "You should not be here!");
		break;
	}

	bx::strCopy(filePath, BX_COUNTOF(filePath), "Assets/Shaders/");
	bx::strCat(filePath, BX_COUNTOF(filePath), _name);
	Path finalPath = Path(filePath);
	SharedPtr<ShaderFile> shad = ResourceCache::GetInstance().Get<ShaderFile>(finalPath);
	const bgfx::Memory* memm = bgfx::copy(shad->Data.data(), static_cast<uint32_t>(shad->Data.size() - 1));
	bgfx::ShaderHandle handle = bgfx::createShader(memm);
	bgfx::setName(handle, _name);

	return handle;
}

bgfx::ProgramHandle Moonlight::LoadProgram(const char* vsName, const char* fsName)
{
	bgfx::ShaderHandle vertexShader = LoadShader(vsName);
	bgfx::ShaderHandle fragmentShader = BGFX_INVALID_HANDLE;
	if (fsName)
	{
		fragmentShader = LoadShader(fsName);
	}

	return bgfx::createProgram(vertexShader, fragmentShader, true);
}

bx::AllocatorI* Moonlight::getDefaultAllocator()
{
	BX_PRAGMA_DIAGNOSTIC_PUSH();
	BX_PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4459); // warning C4459: declaration of 's_allocator' hides global declaration
	BX_PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wshadow");
	static bx::DefaultAllocator s_allocator;
	return &s_allocator;
	BX_PRAGMA_DIAGNOSTIC_POP();
}