#pragma once
#include "Core/CoreTypes.h"

struct FAtlasUV
{
	uint32 ArrayIndex;

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
	virtual FAtlasUV GetAtlasUV(uint32 internalIndex) = 0;
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
	virtual FAtlasUV GetAtlasUV(uint32 internalIndex) { return FAtlasUV(); };
	virtual void ReleaseUV(uint32 Handle) {};
	virtual void BroadCastEntries() {};
};

class FGridUVManager : public FUVManagerBase
{
public:
	void Initialize(uint32 InAtlasSize, uint32 InMinBlockSize)
	{
		FUVManagerBase::Initialize(InAtlasSize);

		AtlasSize = InAtlasSize;
		MinBlockSize = InMinBlockSize;

		GridCount = AtlasSize / MinBlockSize;
		Occupied.assign(GridCount * GridCount, false);

		NextHandle = 1;
		Entries.clear();
	}

	virtual bool GetHandle(float TextureSize, uint32& OutHandle) override
	{
		const uint32 RequestSize = static_cast<uint32>(std::ceil(TextureSize));
		const uint32 BlockCount = CeilDiv(RequestSize, MinBlockSize);

		if (BlockCount == 0 || BlockCount > GridCount)
			return false;

		uint32 OutX = 0;
		uint32 OutY = 0;

		if (!FindFreeRect(BlockCount, BlockCount, OutX, OutY))
			return false;

		MarkRect(OutX, OutY, BlockCount, BlockCount, true);

		const uint32 Handle = NextHandle++;

		FEntry Entry;
		Entry.X = OutX;
		Entry.Y = OutY;
		Entry.W = BlockCount;
		Entry.H = BlockCount;
		Entry.ArrayIndex = 0;

		Entries.emplace(Handle, Entry);

		OutHandle = Handle;
		return true;
	}

	virtual FAtlasUV GetAtlasUV(uint32 InternalIndex) override
	{
		auto It = Entries.find(InternalIndex);
		if (It == Entries.end())
			return {};

		const FEntry& Entry = It->second;

		const float PixelX1 = static_cast<float>(Entry.X * MinBlockSize);
		const float PixelY1 = static_cast<float>(Entry.Y * MinBlockSize);
		const float PixelX2 = static_cast<float>((Entry.X + Entry.W) * MinBlockSize);
		const float PixelY2 = static_cast<float>((Entry.Y + Entry.H) * MinBlockSize);

		FAtlasUV UV;
		UV.ArrayIndex = Entry.ArrayIndex;
		UV.u1 = PixelX1 / AtlasSize;
		UV.v1 = PixelY1 / AtlasSize;
		UV.u2 = PixelX2 / AtlasSize;
		UV.v2 = PixelY2 / AtlasSize;

		return UV;
	}

	virtual void ReleaseUV(uint32 Handle) override
	{
		auto It = Entries.find(Handle);
		if (It == Entries.end())
			return;

		const FEntry& Entry = It->second;

		MarkRect(Entry.X, Entry.Y, Entry.W, Entry.H, false);
		Entries.erase(It);
	}

	virtual void BroadCastEntries() override
	{
		// Atlas 크기가 바뀌었을 때 기존 UV를 다시 계산해야 하는 구조라면 여기서 알림.
		// 지금 단순 버전에서는 GetAtlasUV()가 매번 현재 AtlasSize 기준으로 계산하므로 비워둬도 됨.
	}

private:
	struct FEntry
	{
		uint32 X = 0;
		uint32 Y = 0;
		uint32 W = 0;
		uint32 H = 0;

		uint32 ArrayIndex = 0;
	};

private:
	static uint32 CeilDiv(uint32 A, uint32 B)
	{
		return (A + B - 1) / B;
	}

	uint32 Index(uint32 X, uint32 Y) const
	{
		return Y * GridCount + X;
	}

	bool IsFreeRect(uint32 X, uint32 Y, uint32 W, uint32 H) const
	{
		if (X + W > GridCount || Y + H > GridCount)
			return false;

		for (uint32 yy = Y; yy < Y + H; ++yy)
		{
			for (uint32 xx = X; xx < X + W; ++xx)
			{
				if (Occupied[Index(xx, yy)])
					return false;
			}
		}

		return true;
	}

	bool FindFreeRect(uint32 W, uint32 H, uint32& OutX, uint32& OutY) const
	{
		for (uint32 y = 0; y + H <= GridCount; ++y)
		{
			for (uint32 x = 0; x + W <= GridCount; ++x)
			{
				if (IsFreeRect(x, y, W, H))
				{
					OutX = x;
					OutY = y;
					return true;
				}
			}
		}

		return false;
	}

	void MarkRect(uint32 X, uint32 Y, uint32 W, uint32 H, bool bOccupied)
	{
		for (uint32 yy = Y; yy < Y + H; ++yy)
		{
			for (uint32 xx = X; xx < X + W; ++xx)
			{
				Occupied[Index(xx, yy)] = bOccupied;
			}
		}
	}

private:
	uint32 AtlasSize = 4096;
	uint32 MinBlockSize = 1024;
	uint32 GridCount = 4;

	uint32 NextHandle = 1;

	std::vector<bool> Occupied;
	std::unordered_map<uint32, FEntry> Entries;
};