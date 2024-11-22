#pragma once
#include "Texture.h"
#include "Geometry.h"
#include "SkyManager.h"
#include <fstream>

// Need to move this
static std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("failed to open file!");
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

struct UniformCameraObject {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 cameraPosition;
    glm::vec4 cameraParams;

    static VkDescriptorSetLayoutBinding getLayoutBinding(uint32_t bind)
    {
        VkDescriptorSetLayoutBinding uboLayoutBinding = {};
        uboLayoutBinding.binding = bind;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
        uboLayoutBinding.pImmutableSamplers = nullptr;

        return uboLayoutBinding;
    }
};

struct UniformModelObject {
    glm::mat4 model;
    glm::mat4 invTranspose;

    static VkDescriptorSetLayoutBinding getLayoutBinding(uint32_t bind)
    {
        VkDescriptorSetLayoutBinding uboLayoutBinding = {};
        uboLayoutBinding.binding = bind;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        uboLayoutBinding.pImmutableSamplers = nullptr;

        return uboLayoutBinding;
    }
};

struct UniformStorageImageObject {
    // This doesn't actually store anything, the destination texture should be set to be the texture of the scene's background image
    static VkDescriptorSetLayoutBinding getLayoutBinding(uint32_t bind)
    {
        VkDescriptorSetLayoutBinding storageImageBinding = {};
        storageImageBinding.binding = bind;
        storageImageBinding.descriptorCount = 1;
        storageImageBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        storageImageBinding.pImmutableSamplers = nullptr;
        storageImageBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        return storageImageBinding;
    }
};

class Shader: public VulkanObject
{
protected:
    // All shaders have layouts and pipelines to delete.
    virtual void cleanup();

    // Different shaders will have different numbers of uniform buffers, need to implement cleanup for each.
    virtual void cleanupUniforms() = 0;

    virtual void createDescriptorSetLayout() = 0;
    virtual void createDescriptorPool() = 0;
    virtual void createDescriptorSet() = 0;
    virtual void createUniformBuffer() = 0;
    virtual void createPipeline() = 0;

    VkShaderModule createShaderModule(const std::vector<char>& code, VkDevice device);

    std::vector<std::string> shaderFilePaths;

    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSet;

    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;

    // Textures to be used for samplers
    std::vector<Texture*> textures;
    std::vector<Texture3D*> textures3D;
    // Renderpass will handle attachments like depth/stencil, MUST initialize externally
    VkRenderPass* renderPass;
    // View size
    VkExtent2D extent;

public:
    // all shaders need a setup function, but will have variable # arguments...
    // for now: make part of constructor

    Shader(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue, VkExtent2D extent) : 
        VulkanObject(device, physicalDevice, commandPool, queue) {
        this->extent = extent;
    }
    virtual ~Shader() {
        cleanup();
    }

    // Must set the render pass for shaders before pipeline creation.
    void setRenderPass(VkRenderPass* renderPass) { this->renderPass = renderPass; }
    
    // Samplers must be initialized before pipeline / descriptor creation.
    void addTexture(Texture* tex) { textures.push_back(tex); }
    void addTexture3D(Texture3D* tex) { textures3D.push_back(tex); }

    virtual void bindShader(VkCommandBuffer& commandBuffer) = 0;
};

// TODO: only albedo for the moment,
// 1. Add cook torrance code for ROUGH_METAL_AO
// 2. Add normal mapping support
// 3. Emissive?
// 4. Find ways to pack alpha channels?
enum PBRTEXTURES
{
    ALBEDO = 0, ROUGH_METAL_AO_HEIGHT, NORMAL
};

/*
* A shader pipeline to rasterize a mesh.
*/
class MeshShader : public Shader
{
private:
    
protected:
    virtual void createDescriptorSetLayout();
    virtual void createDescriptorPool();
    virtual void createDescriptorSet();

    virtual void createUniformBuffer();

    virtual void createPipeline();

    UniformCameraObject cameraUniforms;
    UniformModelObject modelUniforms;
    UniformSunObject sunUniforms;
    UniformSkyObject skyUniforms;

    VkBuffer uniformCameraBuffer;
    VkDeviceMemory uniformCameraBufferMemory;
    VkBuffer uniformModelBuffer;
    VkDeviceMemory uniformModelBufferMemory;
    VkBuffer uniformSunBuffer;
    VkDeviceMemory uniformSunBufferMemory;
    VkBuffer uniformSkyBuffer;
    VkDeviceMemory uniformSkyBufferMemory;

    virtual void cleanupUniforms();
public:
    void setupShader(std::string vertPath, std::string fragPath) {
        shaderFilePaths.push_back(vertPath);
        shaderFilePaths.push_back(fragPath);

        createDescriptorSetLayout();
        createPipeline();
        createUniformBuffer();
        createDescriptorPool();
        createDescriptorSet();
    }
    
    MeshShader(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue, VkExtent2D extent) : Shader(device, physicalDevice, commandPool, queue, extent) {}
    MeshShader(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue, VkExtent2D extent, VkRenderPass *renderPass, std::string vertPath, std::string fragPath, Texture* tex, Texture* pbrTex, Texture* normalTex, Texture* coverageTex, Texture3D* loResCloudShape) :
        Shader(device, physicalDevice, commandPool, queue, extent) {
        this->renderPass = renderPass;
        addTexture(tex);
        addTexture(pbrTex);
        addTexture(normalTex);
        addTexture(coverageTex);
        addTexture3D(loResCloudShape);
        setupShader(vertPath, fragPath);
    }

    virtual ~MeshShader() { cleanupUniforms(); }

    void updateUniformBuffers(UniformCameraObject cam, UniformModelObject model, UniformSunObject sun, UniformSkyObject sky) {
        void* data;
        vkMapMemory(device, uniformCameraBufferMemory, 0, sizeof(cam), 0, &data);
        memcpy(data, &cam, sizeof(cam));
        vkUnmapMemory(device, uniformCameraBufferMemory);

        vkMapMemory(device, uniformModelBufferMemory, 0, sizeof(model), 0, &data);
        memcpy(data, &model, sizeof(model));
        vkUnmapMemory(device, uniformModelBufferMemory);

        vkMapMemory(device, uniformSunBufferMemory, 0, sizeof(sun), 0, &data);
        memcpy(data, &sun, sizeof(sun));
        vkUnmapMemory(device, uniformSunBufferMemory);

        vkMapMemory(device, uniformSkyBufferMemory, 0, sizeof(sky), 0, &data);
        memcpy(data, &sky, sizeof(sky));
        vkUnmapMemory(device, uniformSkyBufferMemory);
    }

    void bindShader(VkCommandBuffer& commandBuffer) override {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    }
};

/*
  Pipeline for drawing a background quad
*/

class BackgroundShader : public Shader
{
private:

protected:
    virtual void createDescriptorSetLayout();
    virtual void createDescriptorPool();
    virtual void createDescriptorSet();

    virtual void createUniformBuffer();

    virtual void createPipeline();

    virtual void cleanupUniforms();

    VkDescriptorSet descriptorSetB; // draws a different texture every other frame
    bool swappedBuffers = false;
public:
    void setupShader(std::string vertPath, std::string fragPath) {
        shaderFilePaths.push_back(vertPath);
        shaderFilePaths.push_back(fragPath);

        createDescriptorSetLayout();
        createPipeline();
        createUniformBuffer();
        createDescriptorPool();
        createDescriptorSet();
    }

    BackgroundShader(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue, VkExtent2D extent) : Shader(device, physicalDevice, commandPool, queue, extent) {}
    BackgroundShader(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue, VkExtent2D extent, VkRenderPass *renderPass, std::string vertPath, std::string fragPath, Texture* texA, Texture* texB) :
        Shader(device, physicalDevice, commandPool, queue, extent) {
        this->renderPass = renderPass;
        addTexture(texA);
        addTexture(texB);
        setupShader(vertPath, fragPath);
        swappedBuffers = false;
    }

    virtual ~BackgroundShader() { cleanupUniforms(); }

    void bindShader(VkCommandBuffer& commandBuffer) override {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        if (swappedBuffers) {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSetB, 0, nullptr);
        }
        else {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
        }
        
        swappedBuffers = !swappedBuffers;
    }
};

/*
  Pipeline for computing clouds
*/

class ComputeShader : public Shader
{
private:

protected:
    virtual void createDescriptorSetLayout();
    virtual void createDescriptorPool();
    virtual void createDescriptorSet();

    virtual void createUniformBuffer();

    virtual void createPipeline();

    virtual void cleanupUniforms();

    UniformStorageImageObject storageImageUniform;
    UniformStorageImageObject storageImageUniformPrev;
    UniformCameraObject cameraUniforms;

    VkBuffer uniformCameraBuffer;
    VkDeviceMemory uniformCameraBufferMemory;
    VkBuffer uniformCameraBufferPrev;
    VkDeviceMemory uniformCameraBufferMemoryPrev;

    VkBuffer uniformSunBuffer;
    VkDeviceMemory uniformSunBufferMemory;
    VkBuffer uniformSkyBuffer;
    VkDeviceMemory uniformSkyBufferMemory;
    VkBuffer uniformCloudRenderBuffer;
    VkDeviceMemory uniformCloudRenderBufferMemory;

    // need sets to ping-pong image buffers
    VkDescriptorSetLayout storageSetLayout;
    VkDescriptorSet storageBufferSetA;
    VkDescriptorSet storageBufferSetB;

    void createStorageSetLayout();
    void createStorageDescriptorSets();

    bool swappedBuffers = false;
public:
    void setupShader(std::string path) {
        shaderFilePaths.push_back(path);

        createDescriptorSetLayout();
        createStorageSetLayout();
        createPipeline();
        createUniformBuffer();
        createDescriptorPool();
        createDescriptorSet();
        createStorageDescriptorSets();
    }

    ComputeShader(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue, VkExtent2D extent) : Shader(device, physicalDevice, commandPool, queue, extent) {}
    ComputeShader(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue, VkExtent2D extent,
                  VkRenderPass *renderPass, std::string path, Texture* storageTex, Texture* storageTexPrev, Texture* placementTex, Texture* nightSkyTex, Texture* curlTexture,Texture* cirroTexture, Texture3D* lowResCloudShapeTex, Texture3D* hiResCloudShapeTex, Texture3D* sdfCloudShapeTex_01, Texture3D* sdfCloudShapeTex_02) :

        Shader(device, physicalDevice, commandPool, queue, extent) {
        this->renderPass = renderPass;
        // Note: This texture is intended to be written to. In this application, it is set to be the sampled texture of a separate BackgroundShader.
        addTexture(storageTex);
        addTexture(storageTexPrev);
        addTexture(placementTex);
        addTexture(nightSkyTex);
        addTexture(curlTexture);
        addTexture(cirroTexture);
        addTexture3D(lowResCloudShapeTex);
        addTexture3D(hiResCloudShapeTex);
        addTexture3D(sdfCloudShapeTex_01);
        addTexture3D(sdfCloudShapeTex_02);
        setupShader(path);
        swappedBuffers = false;
    }

    virtual ~ComputeShader() { cleanupUniforms(); }

    void updateUniformBuffers(UniformCameraObject& cam, UniformCameraObject& camPrev, UniformSkyObject& sky, UniformSunObject& sun, UniformCloudRendererObject& cloudrenderer);
    void bindShader(VkCommandBuffer& commandBuffer) override {

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        
        if (swappedBuffers) {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &storageBufferSetB, 0, nullptr);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 1, 1, &storageBufferSetA, 0, nullptr);

        }
        else {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &storageBufferSetA, 0, nullptr);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 1, 1, &storageBufferSetB, 0, nullptr);

        }

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 2, 1, &descriptorSet, 0, nullptr);

        swappedBuffers = !swappedBuffers;
    }
};

// Another compute shader for transferring pixels from one background to another
class ReprojectShader : public Shader
{
private:

protected:
    virtual void createDescriptorSetLayout();
    virtual void createDescriptorPool();
    virtual void createDescriptorSet();

    virtual void createUniformBuffer();

    virtual void createPipeline();

    virtual void cleanupUniforms();

    VkDescriptorSet descriptorSetB; // draws to a different texture every other frame
    bool swappedBuffers = false;

    VkDescriptorSetLayout uniformSetLayout;
    VkDescriptorSet uniformSet;

    VkBuffer uniformCameraBuffer;
    VkDeviceMemory uniformCameraBufferMemory;
    VkBuffer uniformCameraBufferPrev;
    VkDeviceMemory uniformCameraBufferMemoryPrev;

    VkBuffer uniformSkyBuffer;
    VkDeviceMemory uniformSkyBufferMemory;

    VkBuffer uniformSunBuffer;
    VkDeviceMemory uniformSunBufferMemory;
public:
    void setupShader(std::string path) {
        shaderFilePaths.push_back(path);

        createDescriptorSetLayout();
        createPipeline();
        createUniformBuffer();
        createDescriptorPool();
        createDescriptorSet();
    }

    ReprojectShader(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue, VkExtent2D extent) : Shader(device, physicalDevice, commandPool, queue, extent) {}
    ReprojectShader(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue, VkExtent2D extent, VkRenderPass *renderPass, std::string shaderPath, Texture* texA, Texture* texB) :
        Shader(device, physicalDevice, commandPool, queue, extent) {
        this->renderPass = renderPass;
        addTexture(texA);
        addTexture(texB);
        setupShader(shaderPath);
        swappedBuffers = false;
    }

    virtual ~ReprojectShader() { cleanupUniforms(); }

    void updateUniformBuffers(UniformCameraObject& cam, UniformCameraObject& camPrev, UniformSkyObject& sky, UniformSunObject& sun);

    void bindShader(VkCommandBuffer& commandBuffer) override {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

        if (swappedBuffers) {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSetB, 0, nullptr);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 1, 1, &descriptorSet, 0, nullptr);
        }
        else {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 1, 1, &descriptorSetB, 0, nullptr);
        }

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 2, 1, &uniformSet, 0, nullptr);

        swappedBuffers = !swappedBuffers;
    }
};

/*
  Pipeline for post processing effects
*/

// This class should be extended by classes for each individual post processing effect, because each will have different uniform setups.
// at the moment, this is identical to the background shader class
class PostProcessShader : public Shader
{
private:

protected:
    virtual void createDescriptorSetLayout();
    virtual void createDescriptorPool();
    virtual void createDescriptorSet();

    virtual void createUniformBuffer();

    virtual void createPipeline();

    virtual void cleanupUniforms();

    VkDescriptorImageInfo* descriptorImageInfo;

    // Uniform buffers and buffer memory eventually
    // ex: gaussian blur parameters, high pass parameters, sun position for radial blur, god rays, etc
    // TODO: make this class's function virtual and override them in the subclass
    // need a GodRayShader class that has these uniforms:
    
    // God ray shader uniforms
    VkBuffer uniformSunBuffer;
    VkDeviceMemory uniformSunBufferMemory;

    VkBuffer uniformCameraBuffer;
    VkDeviceMemory uniformCameraBufferMemory;

public:
    void setupShader(std::string vertPath, std::string fragPath) {
        shaderFilePaths.push_back(vertPath);
        shaderFilePaths.push_back(fragPath);

        createDescriptorSetLayout();
        createPipeline();
        createUniformBuffer();
        createDescriptorPool();
        createDescriptorSet();
    }

    //TODO: change this constructor to take an image descriptor instead of a texture, or somehow create a texture from the framebuffer image descriptor
    PostProcessShader(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue, VkExtent2D extent) : Shader(device, physicalDevice, commandPool, queue, extent) {}
    PostProcessShader(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue, VkExtent2D extent, VkRenderPass *renderPass, std::string vertPath, std::string fragPath, VkDescriptorImageInfo* tex) :
        Shader(device, physicalDevice, commandPool, queue, extent), descriptorImageInfo(tex) {
        this->renderPass = renderPass;
        setupShader(vertPath, fragPath);
    }

    virtual ~PostProcessShader() { cleanupUniforms(); }

    void updateUniformBuffers(UniformCameraObject& cam, UniformSunObject& sun);
    void bindShader(VkCommandBuffer& commandBuffer) override {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    }
};