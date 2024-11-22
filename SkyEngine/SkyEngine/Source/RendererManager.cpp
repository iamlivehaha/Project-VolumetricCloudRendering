#include "RendererManager.h"

RendererManager::RendererManager()
{
	InitCloudRenderer();
}

RendererManager::~RendererManager()
{
}

void RendererManager::InitCloudRenderer()
{
	CreateFloatParams("coverage_rate", 0.85f);
	CreateFloatParams("erosion_rate", 1.0f);
	CreateFloatParams("extinction", 1.2f);
	CreateFloatParams("tempfloat", 0.5f);
	CreateVectorParams("wind_direction", glm::vec4(1.0f,0.05f,1.0f,0.0f));
	CreateVectorParams("cloudinfo1", glm::vec4(1.0f, 1.f, 1.0f, 0.0f));
	CreateVectorParams("cloudinfo2", glm::vec4(0.0f, 0.0f, 0.7f, 0.35f));
	CreateVectorParams("cloudinfo3", glm::vec4(20.0f, 0.0f, 1.0f, 1.0f));
	CreateVectorParams("cloudinfo4", glm::vec4(0, 1.0f, 180.f, 100.0f));
	CreateVectorParams("cloudinfo5", glm::vec4(0, 0, 4, 0.32f));
	CreateVectorParams("tempVector", glm::vec4(0.0f, 1500.0f, 1500.0f, 0.0f));
}

void RendererManager::CreateFloatParams(const char* name, float value)
{
	floatmap.insert(std::make_pair(name, value));
}

void RendererManager::CreateStringParams(const char* name, const char* value)
{
	stringmap.insert(std::make_pair(name, value));
}


void RendererManager::CreateVectorParams(const char* name, glm::vec4 value)
{
	vectormap.insert(std::make_pair(name, value));
}

void RendererManager::SetFloatParams(const char* name, float value)
{
	floatmap[name] = value;
}

void RendererManager::SetStringParams(const char* name, const char* value)
{
	stringmap[name] = value;
}

void RendererManager::SetVectorParams(const char* name, glm::vec4 value)
{
	vectormap[name] = value;
}

float& RendererManager::GetFloatParams(const char* name)
{
	// TODO: insert return statement here
	return floatmap[name];
}

const char* RendererManager::GetStringParams(const char* name)
{
	return stringmap[name];
}

glm::vec4& RendererManager::GetVectorParams(const char* name)
{
	// TODO: insert return statement here
	return vectormap[name];
}
