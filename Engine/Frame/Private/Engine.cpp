#include "..\Public\Engine.h"
#include "..\Public\Renderer.h"
#include "..\Public\Globals.h"
#include "..\..\Platforms\Windows\Public\RenderWindowWindows.h"

IDevice* RealEngine::Device = nullptr;

int RealEngine::Init()
{
	return 0;
}

int RealEngine::Tick()
{
	if (Renderer != nullptr)
	{
		Renderer->Render();
	}

	return 0;
}

int RealEngine::Exit()
{
	if (Renderer != nullptr)
	{
		delete Renderer;
	}

	if (RenderTarget != nullptr)
	{
		delete RenderTarget;
	}

	if (Device != nullptr)
	{
		delete Device;
	}

	return 0;
}

IDevice* RealEngine::GetDevice()
{
	return Device;
}
