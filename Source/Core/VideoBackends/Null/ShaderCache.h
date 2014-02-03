// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <map>
#include <memory>

#include "VideoCommon/GeometryShaderGen.h"
#include "VideoCommon/PixelShaderGen.h"
#include "VideoCommon/VertexShaderGen.h"
#include "VideoCommon/VideoCommon.h"

namespace Null
{

template <typename Uid>
class ShaderCache
{
public:
	ShaderCache();
	virtual ~ShaderCache();

	void Clear();
	bool SetShader(DSTALPHA_MODE dstAlphaMode, u32 primitive_type);

protected:
	virtual Uid GetUid(DSTALPHA_MODE dstAlphaMode, u32 primitive_type, API_TYPE api_type) = 0;
	virtual ShaderCode GenerateCode(DSTALPHA_MODE dstAlphaMode, u32 primitive_type, API_TYPE api_type) = 0;

private:
	using SCache = std::map<Uid, std::string>;

	SCache s_shaders;
	const std::string* s_last_entry = nullptr;
	Uid s_last_uid;
	UidChecker<Uid, ShaderCode> s_uid_checker;
};

class VertexShaderCache
: public ShaderCache<VertexShaderUid>
{
public:
	static std::unique_ptr<VertexShaderCache> s_instance;

protected:
	VertexShaderUid GetUid(DSTALPHA_MODE dstAlphaMode, u32 primitive_type, API_TYPE api_type) override
	{
		return GetVertexShaderUid(api_type);
	}
	ShaderCode GenerateCode(DSTALPHA_MODE dstAlphaMode, u32 primitive_type, API_TYPE api_type) override
	{
		return GenerateVertexShaderCode(api_type);
	}
};

class GeometryShaderCache
: public ShaderCache<GeometryShaderUid>
{
public:
	static std::unique_ptr<GeometryShaderCache> s_instance;

protected:
	GeometryShaderUid GetUid(DSTALPHA_MODE dstAlphaMode, u32 primitive_type, API_TYPE api_type) override
	{
		return GetGeometryShaderUid(primitive_type, api_type);
	}
	ShaderCode GenerateCode(DSTALPHA_MODE dstAlphaMode, u32 primitive_type, API_TYPE api_type) override
	{
		return GenerateGeometryShaderCode(primitive_type, api_type);
	}
};

class PixelShaderCache
: public ShaderCache<PixelShaderUid>
{
public:
	static std::unique_ptr<PixelShaderCache> s_instance;

protected:
	PixelShaderUid GetUid(DSTALPHA_MODE dstAlphaMode, u32 primitive_type, API_TYPE api_type) override
	{
		return GetPixelShaderUid(dstAlphaMode, api_type);
	}
	ShaderCode GenerateCode(DSTALPHA_MODE dstAlphaMode, u32 primitive_type, API_TYPE api_type) override
	{
		return GeneratePixelShaderCode(dstAlphaMode, api_type);
	}
};


}  // namespace NULL
