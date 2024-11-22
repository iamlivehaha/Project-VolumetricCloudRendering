#pragma once
//#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/vec3.hpp>
#include <glm/mat3x3.hpp>
#include <algorithm>
#include <map> 

class RendererManager
{
private:
	std::map<const char*, float> floatmap;
	std::map<const char*, glm::vec4> vectormap;
	std::map<const char*, const char*> stringmap;

public:
	RendererManager();
	~RendererManager();
	void InitCloudRenderer();
	void CreateFloatParams(const char* name, float value);
	void CreateStringParams(const char* name, const char* value);
	void CreateVectorParams(const char* name, glm::vec4 value);
	void SetFloatParams(const char* name, float value);
	void SetStringParams(const char* name, const char* value);
	void SetVectorParams(const char* name, glm::vec4 value);
	float& GetFloatParams(const char* name);
	const char* GetStringParams(const char* name);
	glm::vec4& GetVectorParams(const char* name);
	bool IsFloatEmpty() { return floatmap.empty(); };
	bool IsVectorEmpty() { return vectormap.empty(); };
	bool IsEmpty() { return IsFloatEmpty() || IsVectorEmpty(); };
};


