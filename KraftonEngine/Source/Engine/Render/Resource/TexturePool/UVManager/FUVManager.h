#pragma once
#include "Core/CoreTypes.h"

struct AtlasUV
{
	float u1;
	float v1;
	float u2;
	float v2;
};

class FUVManagerBase
{
public:
	void Initialize(uint32 InSize) { Size = InSize; }
	virtual bool GetHandle(float TextureSize, uint32& OutHandle) = 0;
	virtual AtlasUV GetAtlasUV(uint32 internalIndex) = 0;
	virtual void ReleaseUV(uint32 Handle) = 0;
	virtual void BroadCastEntries() = 0;
	virtual void SetSize(uint32 InNewTextureSize)
	{
		Size = InNewTextureSize;
		BroadCastEntries();
	}

private:
	uint32 Size;
};

class TempManager : public FUVManagerBase
{
public:
	virtual bool GetHandle(float TextureSize, uint32& OutHandle) { return false; };
	virtual AtlasUV GetAtlasUV(uint32 internalIndex) { return AtlasUV(); };
	virtual void ReleaseUV(uint32 Handle) {};
	virtual void BroadCastEntries() {};
};

