#include "VulkanApplication.h"
#include <sstream>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

static void check_vk_result(VkResult err)
{
	if (err == 0)
		return;
	fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
	if (err < 0)
		abort();
}

/// --- callback proxy functions
VkResult CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback) {
	auto func = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");
	if (func != nullptr) {
		return func(instance, pCreateInfo, pAllocator, pCallback);
	}
	else {
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	}
}

void DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator) {
	auto func = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
	if (func != nullptr) {
		func(instance, callback, pAllocator);
	}
}

/// empty for now
VulkanApplication::VulkanApplication()
{
}

VulkanApplication::~VulkanApplication()
{
}

void VulkanApplication::initWindow() {
	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	//glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

	window = glfwCreateWindow(WIDTH, HEIGHT, "Sky Engine", nullptr, nullptr);

	glfwSetWindowUserPointer(window, this);
	glfwSetWindowSizeCallback(window, VulkanApplication::onWindowResized);

	//mainCamera = Camera(glm::vec3(0.f, 1000.f, 1.f), glm::vec3(10000.f, 10000.f, 0.f), 0.1f, 10000.0f, 45.0f);
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

}

void VulkanApplication::initVulkan() {

	rendererSystem = RendererManager();

	createInstance();
#ifdef _DEBUG
	setupDebugCallback();
#endif
	createSurface();
	pickPhysicalDevice();
	createLogicalDevice();
	createSwapChain();
	createImageViews();

	createRenderPass();

	createCommandPool();

	initializeTextures();

	createFramebuffers(); // must come after so depth texture is initialized

	setupOffscreenPass();

	initializeGeometry();

	initializeShaders();

	init_imgui(window, VK_FORMAT_B8G8R8A8_UNORM);

	createCommandBuffers();
	createPostProcessCommandBuffer();
	createComputeCommandBuffer();
	createSemaphores();

	mainCamera = Camera(glm::vec3(0.f, 1.f, 1.f), glm::vec3(-1.f, 1.f, 0.f), 0.1f, 1000.0f, 45.0f);
	mainCamera.setAspect((float)swapChainExtent.width, (float)swapChainExtent.height);
	skySystem = SkyManager();
}
void VulkanApplication::init_imgui(GLFWwindow* window, VkFormat format)
{
	// create Renderpass for imgui
	VkAttachmentDescription colorAttachment{};
	colorAttachment.format = format;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference colorReference{};
	colorReference.attachment = 0;
	colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpassDescription{};
	subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDescription.colorAttachmentCount = 1;
	subpassDescription.pColorAttachments = &colorReference;

	VkSubpassDependency supassDependency{};
	supassDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	supassDependency.dstSubpass = 0;
	supassDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	supassDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	supassDependency.srcAccessMask = 0;
	supassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo renderPassCreateInfo{};
	renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassCreateInfo.attachmentCount = 1;
	renderPassCreateInfo.pAttachments = &colorAttachment;
	renderPassCreateInfo.subpassCount = 1;
	renderPassCreateInfo.pSubpasses = &subpassDescription;
	renderPassCreateInfo.dependencyCount = 1;
	renderPassCreateInfo.pDependencies = &supassDependency;

	vkCreateRenderPass(device, &renderPassCreateInfo, nullptr, &imgui_renderPass);

	//create command pool and command buffer and framebuffer
	{
		uint32_t count = swapChainImageViews.size();
		imgui_CommandBuffers.resize(count);

		createCommandPool(&imgui_CommandPool, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
		for (size_t i = 0; i < count; i++) {
			//createCommandPool(&imGuiCommandPools[i], VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
			createCommandBuffers(&imgui_CommandBuffers[i], 1, imgui_CommandPool);
		}

		initImguiFrameBuffer();
	}


	//create Semaphore
	{
		CreateImguiSemaphore(&Simgui_renderComplete);
		CreateImguiSemaphore(&Simgui_presentComplete);
	}

	// Create Descriptor Pool

	VkAllocationCallbacks* g_Allocator = NULL;
	{

		VkResult err;
		VkDescriptorPoolSize pool_sizes[] =
		{
			{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
			{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
			{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
		};
		VkDescriptorPoolCreateInfo pool_info = {};
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
		pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
		pool_info.pPoolSizes = pool_sizes;
		err = vkCreateDescriptorPool(device, &pool_info, g_Allocator, &imgui_DescriptorPool);
		check_vk_result(err);
	}


	//这里使用了imgui的一个分支docking
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	(void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;        // Enable Gamepad Controls

	//look for docking branch in github for better support
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platfor

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsLight();

	// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
	ImGuiStyle& style = ImGui::GetStyle();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}

	QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
	uint32_t g_QueueFamily = indices.graphicsFamily;

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForVulkan(window, true);
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = instance;
	init_info.PhysicalDevice = physicalDevice;
	init_info.Device = device;
	init_info.QueueFamily = g_QueueFamily;
	init_info.Queue = graphicsQueue;
	init_info.PipelineCache = VK_NULL_HANDLE;
	init_info.DescriptorPool = imgui_DescriptorPool;
	init_info.Subpass = 0;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	init_info.Allocator = VK_NULL_HANDLE;
	init_info.CheckVkResultFn = check_vk_result;
	ImGui_ImplVulkan_Init(&init_info, imgui_renderPass);
	// Upload Fonts
	{
		// Use any command queue
		VkCommandBuffer command_buffer = beginSingleTimeCommands();
		ImGui_ImplVulkan_CreateFontsTexture(command_buffer);
		endSingleTimeCommands(command_buffer);

		VkResult err = vkDeviceWaitIdle(device);
		check_vk_result(err);
		ImGui_ImplVulkan_DestroyFontUploadObjects();
	}
}

void VulkanApplication::draw_imgui()
{
	VkResult err = vkResetCommandPool(device, imgui_CommandPool, 0);
	check_vk_result(err);

	uint32_t id = swapBackgroundImages ? 1 : 0;

	VkCommandBufferBeginInfo commandbufferinfo = {};
	commandbufferinfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	commandbufferinfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(imgui_CommandBuffers[id], &commandbufferinfo);

	VkClearValue cv;
	cv.color = { 0.0f,0.0f,0.0f,0.0f };

	VkRenderPassBeginInfo rpbi = {};
	rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpbi.framebuffer = imgui_frameBuffers[id];
	rpbi.renderPass = imgui_renderPass;
	rpbi.renderArea.offset = { 0,0 };
	rpbi.renderArea.extent = { uint32_t(WIDTH),uint32_t(HEIGHT) };
	rpbi.clearValueCount = 1;
	rpbi.pClearValues = &cv;
	vkCmdBeginRenderPass(imgui_CommandBuffers[id], &rpbi, VK_SUBPASS_CONTENTS_INLINE);


	ImGuiIO& io = ImGui::GetIO();
	(void)io;
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
	static bool show_demo_window = false;
	static bool show_another_window = true;
	const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(main_viewport->WorkPos.x + 1650, main_viewport->WorkPos.y + 20), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(550, 680), ImGuiCond_FirstUseEver);
	{
		if (show_demo_window)
		{
			ImGui::ShowDemoWindow(&show_demo_window);
		}
		// 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
		ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
		{
			static float f = 0.0f;

			ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

			ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
			ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
			ImGui::Checkbox("Another Window", &show_another_window);

			ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
			ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color

			//static float count = 1;
			if (ImGui::Button("Reset Camera"))
			{// Buttons return true when clicked (most widgets return true when edited/activated)
				mainCamera.setPosition(glm::vec3(0.0, 1.0, 1.0));
				mainCamera.beginTarget(glm::vec3(1, 1, 2));
			}
			ImGui::SameLine();

			ImGui::Text("Camera position: x= %f, y= %f, z=%f", mainCamera.getPosition().x, mainCamera.getPosition().y, mainCamera.getPosition().z);               // Display some text (you can use a format strings too)

			//物理设备包含一个特殊值，它指示时间戳查询增加 1 所花费的纳秒数。这对于将查询结果解释为纳秒或毫秒是必要的
			VkPhysicalDeviceProperties queryProperties;
			vkGetPhysicalDeviceProperties(physicalDevice, &queryProperties);
			float timestampPeriod = queryProperties.limits.timestampPeriod;//1 ns

			ImGui::Text("timestampPeriod = %f ns", timestampPeriod);
			ImGui::Text("Current Deivce: %s", queryProperties.deviceName);
			ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
			ImGui::End();
		}

		if (show_another_window)
		{
			draw_CloudRenderPanel(&show_another_window);
		}
	}
	ImGui::Render();
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), imgui_CommandBuffers[id], 0);

	vkCmdEndRenderPass(imgui_CommandBuffers[id]);
	vkEndCommandBuffer(imgui_CommandBuffers[id]);

	// Update and Render additional Platform Windows
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
	}

}

void VulkanApplication::draw_CloudRenderPanel(bool* enable)
{
	static bool show_clound_Modeling = true;
	static bool show_clound_Lighting = true;
	static bool show_cloud_Rendering = true;

	ImGui::Begin("Cloud Rendering");   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
	ImGui::Text("This is a cloud Renderer panel!");

	// Post-baking font scaling. Note that this is NOT the nice way of scaling fonts, read below.
	// (we enforce hard clamping manually as by default DragFloat/SliderFloat allows CTRL+Click text to get out of bounds).
	ImGuiIO& io = ImGui::GetIO();
	const float MIN_SCALE = 0.3f;
	const float MAX_SCALE = 2.0f;
	static float window_scale = 1.0f;
	ImGui::SetWindowFontScale(1.8f);
	ImGui::PushItemWidth(ImGui::GetFontSize() * 8);
	if (ImGui::DragFloat("window scale", &window_scale, 0.005f, MIN_SCALE, MAX_SCALE, "%.2f", ImGuiSliderFlags_AlwaysClamp)) // Scale only this window
		ImGui::SetWindowFontScale(window_scale);
	ImGui::DragFloat("global scale", &io.FontGlobalScale, 0.005f, MIN_SCALE, MAX_SCALE, "%.2f", ImGuiSliderFlags_AlwaysClamp); // Scale everything

	const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(main_viewport->WorkPos.x, main_viewport->WorkPos.y + 20), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(550, 680), ImGuiCond_FirstUseEver);

	if (rendererSystem.IsEmpty())
	{
		throw std::runtime_error("renderer system is not ready for data control!");
		return;
	}



	static float tempVector[4] = { -8000.0f, 9000.0f, -8000.0f, 0.0f };
	static float tempMin = 0;
	static float tempMax = 4.0;

	ImGui::InputFloat("tempfloatMin", &tempMin, 0.000001f, 1.0f, "%.8f");
	ImGui::InputFloat("tempfloatMax", &tempMax, 0.000001f, 1.0f, "%.8f");
	ImGui::SliderFloat("tempfloat", &rendererSystem.GetFloatParams("tempfloat"), tempMin, tempMax, "%.8f");
	ImGui::SetNextItemWidth(800);
	ImGui::InputFloat4("tempVector", tempVector);

	rendererSystem.SetVectorParams("tempVector", glm::vec4(tempVector[0], tempVector[1], tempVector[2], tempVector[3]));

	if (show_clound_Modeling)             ShowModelingPanel(&show_clound_Modeling);
	if (show_clound_Lighting)                 ShowLightingPanel(&show_clound_Lighting);
	if (show_cloud_Rendering)              ShowRenderingPanel(&show_cloud_Rendering);

	if (ImGui::Button("Close Me"))
		*enable = false;
	ImGui::End();
}

void VulkanApplication::ShowModelingPanel(bool* enable)
{
	static float windDir[4] = { 1.0f,0.05f,1.0f,0.8f };
	static float cirrusWindDir[4] = { 1.0f,0.05f,1.0f,0.0f };
	static float cloudinfo1[4] = { 0.0f,0.7f,1.0f,1.0f };
	static float cloudinfo3[4] = { 5.0f,0.0f,1.0f,1.0f };

	if (ImGui::TreeNode("Modeling"))
	{
		ImGui::SeparatorText("Basic Setting");
		ImGui::SliderFloat("Extinction Value", &rendererSystem.GetFloatParams("extinction"), 1.0f, 2.0f);
		ImGui::SliderFloat("Precipitation Rate", &cloudinfo3[1], 0.0f, 1.0f);

		ImGui::SliderFloat("coverage", &rendererSystem.GetFloatParams("coverage_rate"), 0.0f, 1.0f);
		ImGui::SliderFloat("erosion", &rendererSystem.GetFloatParams("erosion_rate"), 0.0f, 1.0f);


		ImGui::SeparatorText("Troposphere Layer Setting");
		ImGui::SliderFloat("stratus_rate", &cloudinfo1[0], 0.0f, 1.0f);
		ImGui::SliderFloat("cumulus_rate", &cloudinfo1[1], 0.0f, 1.0f);
		ImGui::SliderFloat("altocumulus_rate", &cloudinfo1[2], 0.0f, 1.0f);

		ImGui::SliderFloat("Global Wind Strength", &cloudinfo3[0], 0.0f, 50.0f);
		ImGui::SetNextItemWidth(800);
		ImGui::InputFloat4("Global wind_direction", windDir);
		ImGui::SliderFloat("Local Wind Strength", &cloudinfo1[3], 0.0f, 10.0f);


		ImGui::SeparatorText("Cirrus Layer Setting");
		ImGui::SliderFloat("cirrusCloudType", &cirrusWindDir[3], 0.0f, 1.0f);
		ImGui::SetNextItemWidth(800);
		ImGui::InputFloat4("cirrusWind_direction", cirrusWindDir);

		ImGui::TreePop();
	}

	rendererSystem.SetVectorParams("wind_direction", glm::vec4(windDir[0], windDir[1], windDir[2], windDir[3]));
	rendererSystem.SetVectorParams("cirrusWind_direction", glm::vec4(cirrusWindDir[0], cirrusWindDir[1], cirrusWindDir[2], cirrusWindDir[3]));
	rendererSystem.SetVectorParams("cloudinfo1", glm::vec4(cloudinfo1[0], cloudinfo1[1], cloudinfo1[2], cloudinfo1[3]));
	rendererSystem.SetVectorParams("cloudinfo3", glm::vec4(cloudinfo3[0], cloudinfo3[1], cloudinfo3[2], cloudinfo3[3]));

}

void VulkanApplication::ShowLightingPanel(bool* enable)
{
	static float cloudinfo2[4] = { 0.0f,0.0f,0.7f,0.35f };
	static float cloudinfo5[4] = { 0.0f,0.0f,4.f,0.32f };

	if (ImGui::TreeNode("Lighting"))
	{
		ImGui::SeparatorText("Basic Setting");
		ImGui::SliderFloat("sun_intensity", &cloudinfo2[2], 0.03f, 1.0f);
		ImGui::SliderFloat("backgroundColor_rate", &cloudinfo2[3], 0.0f, 1.0f);


		ImGui::SeparatorText("HG Phase Setting");

		ImGui::SliderFloat("sliver_intensity", &cloudinfo2[0], 0.0f, 1.0f);
		ImGui::SliderFloat("sliver_spread", &cloudinfo2[1], 0.0f, 0.99f);


		if (ImGui::TreeNode("Cloud Ambient Color"))
		{
			static int current_ambientcolor = 0;
			const char* ambient_color[] = { "Physical based color","Numerical color" };
			ImGui::SetNextItemWidth(500);
			ImGui::Combo("select ", &current_ambientcolor, ambient_color,2);
			cloudinfo5[0] = current_ambientcolor;

			ImGui::TreePop();
		}

		if (ImGui::TreeNode("Cloud self-shadow"))
		{
			static int current_self_shadow = 0;
			const char* self_shadow[] = { "Secondary ray marching","Shadow map","SDF shadow" };
			ImGui::Text("Warning!!! you must enable SDF Raymarching before using this");
			ImGui::SetNextItemWidth(500);
			ImGui::Combo("select ", &current_self_shadow, self_shadow,3);
			cloudinfo5[1] = current_self_shadow;

			ImGui::SliderFloat("SDF_ShadowSteps", &cloudinfo5[2], 1.f, 15.f);
			ImGui::SliderFloat("LightTangent", &cloudinfo5[3], 0.f, 10.f);

			ImGui::TreePop();
		}
		ImGui::TreePop();
	}

	rendererSystem.SetVectorParams("cloudinfo2", glm::vec4(cloudinfo2[0], cloudinfo2[1], cloudinfo2[2], cloudinfo2[3]));
	rendererSystem.SetVectorParams("cloudinfo5", glm::vec4(cloudinfo5[0], cloudinfo5[1], cloudinfo5[2], cloudinfo5[3]));
}

void VulkanApplication::ShowRenderingPanel(bool* enable)
{
	static float cloudinfo4[4] = { 0,1.0f,180.f,64.0f };

	if (ImGui::TreeNode("Rendering"))
	{

		ImGui::SeparatorText("Basic Setting");
		ImGui::SliderFloat("max_steps", &cloudinfo4[3], 30, 120);

		ImGui::SeparatorText("Cloud RenderMode");
		{
			static int current_rendermode = cloudinfo4[0];
			ImGui::SetNextItemWidth(450);
			ImGui::Combo("select ", &current_rendermode, "ThreePhases Raymarching\0Debug Raymarching\0High-Performance Raymarching\0\0");
			cloudinfo4[0] = current_rendermode;
		}

		ImGui::SeparatorText("SDF Setting");
		ImGui::SliderFloat("sdfboundBoxScaleMax", &cloudinfo4[2], 0.001f, 1000.f);
		ImGui::SliderFloat("sdf_scale", &cloudinfo4[1], 0.0f, 2.0f);

		ImGui::TreePop();
	}

	rendererSystem.SetVectorParams("cloudinfo4", glm::vec4(cloudinfo4[0], cloudinfo4[1], cloudinfo4[2], cloudinfo4[3]));

}



void VulkanApplication::initImguiFrameBuffer()
{
	int count = swapChainImageViews.size();
	std::vector<std::vector<VkImageView>> attachments(count);
	for (int i = 0; i < count; i++)
	{
		attachments[i].push_back(swapChainImageViews[i]);
	}

	//create imgui framebuffer for each swapchain imageview based on imgui_renderpass
	imgui_frameBuffers.resize(count);
	for (uint32_t i = 0; i < count; i++)
	{
		VkFramebufferCreateInfo framebufferInfo = {};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = imgui_renderPass;
		framebufferInfo.attachmentCount = 1;
		framebufferInfo.pAttachments = attachments[i].data();
		framebufferInfo.width = WIDTH;
		framebufferInfo.height = HEIGHT;
		framebufferInfo.layers = 1;

		if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &imgui_frameBuffers[i]) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create framebuffer");
		}
	}
}

void VulkanApplication::DestroyImguiFrameBuffer()
{
	for (int i = 0; i < swapChainImageViews.size(); i++)
	{
		if (imgui_frameBuffers[i] != 0) {
			vkDestroyFramebuffer(device, imgui_frameBuffers[i], nullptr);
		}
	}

}

VkSubmitInfo VulkanApplication::ImguiQueueSubmit(VkSemaphore* waitSemaphore)
{
	draw_imgui();
	uint32_t id = swapBackgroundImages ? 1 : 0;
	static VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = waitSemaphore;
	submitInfo.pWaitDstStageMask = &wait_stage;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &imgui_CommandBuffers[id];
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &Simgui_renderComplete;
	return submitInfo;
}

void VulkanApplication::Cleanup_imgui()
{
	//imgui 
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	vkFreeCommandBuffers(device, imgui_CommandPool, imgui_CommandBuffers.size(), imgui_CommandBuffers.data());
	vkDestroyCommandPool(device, imgui_CommandPool, nullptr);
	DestroyImguiFrameBuffer();
	vkDestroyRenderPass(device, imgui_renderPass, nullptr);
	vkDestroySemaphore(device, Simgui_presentComplete, nullptr);
	vkDestroySemaphore(device, Simgui_renderComplete, nullptr);
	vkDestroyDescriptorPool(device, imgui_DescriptorPool, nullptr);
}

void VulkanApplication::CreateImguiSemaphore(VkSemaphore* semaphore) {
	VkSemaphoreCreateInfo semaphoreInfo = {};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, semaphore) != VK_SUCCESS) {
		printf("create semaphore fail\n");
	}
}

void VulkanApplication::createCommandPool(VkCommandPool* commandPool, VkCommandPoolCreateFlags flags) {

	VkCommandPoolCreateInfo commandPoolCreateInfo = {};
	commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolCreateInfo.queueFamilyIndex = findQueueFamilies(physicalDevice).graphicsFamily;
	commandPoolCreateInfo.flags = flags;

	if (vkCreateCommandPool(device, &commandPoolCreateInfo, nullptr, commandPool) != VK_SUCCESS) {
		throw std::runtime_error("Could not create graphics command pool");
	}
}

void VulkanApplication::createCommandBuffers(VkCommandBuffer* commandBuffer, uint32_t commandBufferCount, VkCommandPool& commandPool) {
	VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
	commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commandBufferAllocateInfo.commandPool = commandPool;
	commandBufferAllocateInfo.commandBufferCount = commandBufferCount;
	vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, commandBuffer);
}

void VulkanApplication::mainLoop() {
	static auto startTime = std::chrono::high_resolution_clock::now();
	prevTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - startTime).count() / 1000.0f;
	deltaTime = prevTime;
	while (!glfwWindowShouldClose(window)) {

		float time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - startTime).count() / 1000.0f;
		deltaTime = time - prevTime;

		glfwPollEvents();
		processInputs();
		updateUniformBuffer();
		drawFrame();

		prevTime = time;
	}
	vkDeviceWaitIdle(device);
}

void VulkanApplication::cleanup() {

	for (size_t i = 0; i < swapChainFramebuffers.size(); i++) {
		vkDestroyFramebuffer(device, swapChainFramebuffers[i], nullptr);
	}
	for (size_t i = 0; i < swapChainImageViews.size(); i++) {
		vkDestroyImageView(device, swapChainImageViews[i], nullptr);
	}
	cleanupOffscreenPass();
	vkDestroyCommandPool(device, commandPool, nullptr);
	vkDestroyCommandPool(device, computeCommandPool, nullptr);
	vkDestroySemaphore(device, renderFinishedSemaphore, nullptr);
	vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);

	//imgui 
	Cleanup_imgui();

	vkDestroyRenderPass(device, renderPass, nullptr);
	vkDestroySwapchainKHR(device, swapChain, nullptr);

	cleanupGeometry();

	cleanupTextures();
	cleanupShaders();

	vkDestroyDevice(device, nullptr);

#ifdef _DEBUG
	DestroyDebugReportCallbackEXT(instance, callback, nullptr);
#endif

	vkDestroySurfaceKHR(instance, surface, nullptr);
	vkDestroyInstance(instance, nullptr);

	glfwDestroyWindow(window);
	glfwTerminate();
}

void VulkanApplication::processInputs() {
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		glfwSetWindowShouldClose(window, true);

	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
		mainCamera.movePosition(Camera::FORWARD, deltaTime);

	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		mainCamera.movePosition(Camera::BACKWARD, deltaTime);

	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		mainCamera.movePosition(Camera::LEFT, deltaTime);

	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		mainCamera.movePosition(Camera::RIGHT, deltaTime);

	if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
		mainCamera.movePosition(Camera::UP, deltaTime);

	if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
		mainCamera.movePosition(Camera::DOWN, deltaTime);

	double xPos, yPos;
	glfwGetCursorPos(window, &xPos, &yPos);

	if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
		mainCamera.mouseRotate(xPos, yPos);
}

void VulkanApplication::drawFrame() {

	// submit compute queue
	// Compute queue submit
	VkSubmitInfo computeSubmitInfo = {};
	computeSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

	computeSubmitInfo.commandBufferCount = 1;
	computeSubmitInfo.pCommandBuffers = &computeCommandBuffers[(swapBackgroundImages ? 1 : 0)];
	swapBackgroundImages = !swapBackgroundImages;

	if (vkQueueSubmit(computeQueue, 1, &computeSubmitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
		throw std::runtime_error("Failed to submit compute command buffer");
	}

	// acquire image from swap chain
	// execute corresponding command buffer
	// return the image to the swap chain, presentation mode
	uint32_t imageIndex;
	VkResult result = vkAcquireNextImageKHR(device, swapChain, std::numeric_limits<uint64_t>::max(), imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

	// must recreate swapchain -or- swap chain isn't working
	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		recreateSwapChain();
		return;
	}
	else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		throw std::runtime_error("failed to acquire swap chain image!");
	}

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

	VkSemaphore waitSemaphores[] = { imageAvailableSemaphore };
	VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = waitSemaphores;
	submitInfo.pWaitDstStageMask = waitStages; // what part of the pipeline is blocked by semaphore; vertex processing can still continue

	// Do all offscreen rendering
	submitInfo.pSignalSemaphores = { &offscreenPass.semaphore };
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &offscreenPass.commandBuffers[(swapBackgroundImages ? 1 : 0)];

	if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
		throw std::runtime_error("failed to submit offscreen command buffer!");
	}

	VkSemaphore signalSemaphores[] = { renderFinishedSemaphore };

	// Draw the scene onto the screen
	submitInfo.pWaitSemaphores = &offscreenPass.semaphore;
	submitInfo.pSignalSemaphores = signalSemaphores;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffers[imageIndex]; // what is executed

	//UI_PASS
	VkSubmitInfo submit_imgui_info = ImguiQueueSubmit(signalSemaphores);
	VkSubmitInfo submit_infos[] = { submitInfo,submit_imgui_info };

	if (vkQueueSubmit(graphicsQueue, _countof(submit_infos), submit_infos, VK_NULL_HANDLE) != VK_SUCCESS) {
		throw std::runtime_error("failed to submit draw command buffer!");
	}


	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = submit_imgui_info.pSignalSemaphores; //replaced by imgui rendercomplete signal
	VkSwapchainKHR swapChains[] = { swapChain };
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapChains;
	presentInfo.pImageIndices = &imageIndex;
	presentInfo.pResults = nullptr; // Optional

	vkQueuePresentKHR(presentQueue, &presentInfo); // present the image

	vkQueueWaitIdle(presentQueue); // sync to avoid mem leaks
}

void VulkanApplication::initializeTextures() {
	meshTexture = new Texture(device, physicalDevice, commandPool, graphicsQueue);
	meshTexture->initFromFile("Textures/grassGround.png");
	meshPBRInfo = new Texture(device, physicalDevice, commandPool, graphicsQueue);
	meshPBRInfo->initFromFile("Textures/rockPBRinfo.png");
	meshNormals = new Texture(device, physicalDevice, commandPool, graphicsQueue);
	meshNormals->initFromFile("Textures/grassGround_Normal.png");
	backgroundTexture = new Texture(device, physicalDevice, commandPool, graphicsQueue, VK_FORMAT_R32G32B32A32_SFLOAT);
	backgroundTexture->initForStorage(swapChainExtent);
	backgroundTexturePrev = new Texture(device, physicalDevice, commandPool, graphicsQueue, VK_FORMAT_R32G32B32A32_SFLOAT);
	backgroundTexturePrev->initForStorage(swapChainExtent);
	depthTexture = new Texture(device, physicalDevice, commandPool, graphicsQueue);
	depthTexture->initForDepthAttachment(swapChainExtent);
	cloudPlacementTexture = new Texture(device, physicalDevice, commandPool, graphicsQueue);
	cloudPlacementTexture->initFromFile("Textures/CloudPlacement.png");
	nightSkyTexture = new Texture(device, physicalDevice, commandPool, graphicsQueue);
	nightSkyTexture->initFromFile("Textures/NightSky/nightSky_noOrange.png");
	cloudCurlNoise = new Texture(device, physicalDevice, commandPool, graphicsQueue);
	cloudCurlNoise->initFromFile("Textures/CurlNoiseFBM.png");
	cloudCirroNoise = new Texture(device, physicalDevice, commandPool, graphicsQueue);
	cloudCirroNoise->initFromFile("Textures/CirroNoise.png");
	lowResCloudShapeTexture3D = new Texture3D(device, physicalDevice, commandPool, graphicsQueue, 128, 128, 128); // 128, 128, 128
	if ((ENABLE_NEW_NOISE))
	{
		lowResCloudShapeTexture3D->initFromFile("Textures/3DTextures/Curly_AlligatorCloudShape/NubisVoxelCloudNoise"); // note: no .png
	}
	else
	{
		lowResCloudShapeTexture3D->initFromFile("Textures/3DTextures/lowResCloudShape/lowResCloud"); // note: no .png
	}
	SDFCloudShapeTexture3D_01 = new Texture3D(device, physicalDevice, commandPool, graphicsQueue, 128, 128, 128); // 128, 128, 128
	SDFCloudShapeTexture3D_01->initFromFile("Textures/3DTextures/SDFCloudShape_01/SDFCloudShape"); // note: no .png

	SDFCloudShapeTexture3D_02 = new Texture3D(device, physicalDevice, commandPool, graphicsQueue, 128, 128, 128); // 128, 128, 128
	SDFCloudShapeTexture3D_02->initFromFile("Textures/3DTextures/SDFCloudShape_02/Cloud_Bake_pighead"); // note: no .png

	hiResCloudShapeTexture3D = new Texture3D(device, physicalDevice, commandPool, graphicsQueue, 32, 32, 32); // 128, 128, 128
	hiResCloudShapeTexture3D->initFromFile("Textures/3DTextures/hiResCloudShape/hiResClouds "); // note: no .png

}

// TODO: management
void VulkanApplication::cleanupTextures() {
	delete meshTexture;
	delete meshPBRInfo;
	delete meshNormals;
	delete backgroundTexture;
	delete backgroundTexturePrev;
	delete depthTexture;
	delete cloudPlacementTexture;
	delete nightSkyTexture;
	delete cloudCurlNoise;
	delete cloudCirroNoise;
	delete lowResCloudShapeTexture3D;
	delete SDFCloudShapeTexture3D_01;
	delete SDFCloudShapeTexture3D_02;
	delete hiResCloudShapeTexture3D;
}

void VulkanApplication::initializeGeometry() {
	sceneGeometry = new Geometry(device, physicalDevice, commandPool, graphicsQueue);
	sceneGeometry->setupFromMesh("Models/terrain.obj");
	backgroundGeometry = new Geometry(device, physicalDevice, commandPool, graphicsQueue);
	backgroundGeometry->setupAsBackgroundQuad();
}

void VulkanApplication::cleanupGeometry() {
	delete sceneGeometry;
	delete backgroundGeometry;
}

void VulkanApplication::initializeShaders() {
	meshShader = new MeshShader(device, physicalDevice, commandPool, graphicsQueue, swapChainExtent,
		&offscreenPass.renderPass, std::string("Shaders/model.vert.spv"), std::string("Shaders/model.frag.spv"), meshTexture, meshPBRInfo, meshNormals, cloudPlacementTexture, lowResCloudShapeTexture3D);

	backgroundShader = new BackgroundShader(device, physicalDevice, commandPool, graphicsQueue, swapChainExtent,
		&offscreenPass.renderPass, std::string("Shaders/background.vert.spv"), std::string("Shaders/background.frag.spv"), backgroundTexture, backgroundTexturePrev);

	// Note: we pass the background shader's texture with the intention of writing to it with the compute shader
	reprojectShader = new ReprojectShader(device, physicalDevice, commandPool, computeQueue, swapChainExtent, &offscreenPass.renderPass,
		std::string("Shaders/reproject.comp.spv"), backgroundTexture, backgroundTexturePrev);

	computeShader = new ComputeShader(device, physicalDevice, commandPool, computeQueue, swapChainExtent,
		&offscreenPass.renderPass, std::string("Shaders/compute-clouds.comp.spv"), backgroundTexture, backgroundTexturePrev, cloudPlacementTexture, nightSkyTexture, cloudCurlNoise, cloudCirroNoise,
		lowResCloudShapeTexture3D, hiResCloudShapeTexture3D,SDFCloudShapeTexture3D_01,SDFCloudShapeTexture3D_02);

	// Post shaders: there will be many
	// This is still offscreen, so the render pass is the offscreen render pass
	godRayShader = new PostProcessShader(device, physicalDevice, commandPool, graphicsQueue, swapChainExtent,
		&offscreenPass.renderPass, std::string("Shaders/post-pass.vert.spv"), std::string("Shaders/god-ray.frag.spv"), &offscreenPass.framebuffers[0].descriptor);

	radialBlurShader = new PostProcessShader(device, physicalDevice, commandPool, graphicsQueue, swapChainExtent,
		&offscreenPass.renderPass, std::string("Shaders/post-pass.vert.spv"), std::string("Shaders/radialBlur.frag.spv"), &offscreenPass.framebuffers[1].descriptor);

	toneMapShader = new PostProcessShader(device, physicalDevice, commandPool, graphicsQueue, swapChainExtent,
		&renderPass, std::string("Shaders/post-pass.vert.spv"), std::string("Shaders/tonemap.frag.spv"), &offscreenPass.framebuffers[2].descriptor);
}

void VulkanApplication::cleanupShaders() {
	delete meshShader;
	delete backgroundShader;
	delete computeShader;
	delete reprojectShader;
	delete toneMapShader;
	delete godRayShader;
	delete radialBlurShader;
}

void VulkanApplication::cleanupOffscreenPass() {
	vkDestroySampler(device, offscreenPass.sampler, nullptr);

	for (auto& framebuffer : offscreenPass.framebuffers)
	{
		// Attachments
		vkDestroyImageView(device, framebuffer.color.view, nullptr);
		vkDestroyImage(device, framebuffer.color.image, nullptr);
		vkFreeMemory(device, framebuffer.color.mem, nullptr);
		vkDestroyImageView(device, framebuffer.depth.view, nullptr);
		vkDestroyImage(device, framebuffer.depth.image, nullptr);
		vkFreeMemory(device, framebuffer.depth.mem, nullptr);

		vkDestroyFramebuffer(device, framebuffer.framebuffer, nullptr);
	}

	vkDestroyRenderPass(device, offscreenPass.renderPass, nullptr);
	vkFreeCommandBuffers(device, commandPool, offscreenPass.commandBuffers.size(), offscreenPass.commandBuffers.data());
	vkDestroySemaphore(device, offscreenPass.semaphore, nullptr);
}

void VulkanApplication::updateUniformBuffer() {
	float time = prevTime + deltaTime;

	UniformCameraObject ucoPrev = {};
	ucoPrev.proj = mainCamera.getProjPrev();
	ucoPrev.proj[1][1] *= -1;
	ucoPrev.view = mainCamera.getViewPrev();
	ucoPrev.cameraPosition = glm::vec4(mainCamera.getPositionPrev(), 1.0f);

	UniformCameraObject uco = {};
	uco.proj = mainCamera.getProj();
	uco.proj[1][1] *= -1; // :(
	uco.view = mainCamera.getView();
	uco.cameraPosition = glm::vec4(mainCamera.getPosition(), 1.0f);
	uco.cameraParams.x = mainCamera.getAspect();
	uco.cameraParams.y = mainCamera.getHTanFov();

	UniformModelObject umo = {};
	umo.model = glm::mat4(1.0f);
	umo.model[0][0] = 100.0f;
	umo.model[2][2] = 100.0f;
	umo.invTranspose = glm::inverse(glm::transpose(umo.model));
	float interp = sin(time * 0.025f);

	skySystem.rebuildSkyFromNewSun(interp * 0.5f, 0.25f);
	skySystem.setTime(time * 10.f);

	UniformSkyObject sky = skySystem.getSky();
	UniformSunObject& sun = skySystem.getSun(); // by reference so we can update the pixel counter in sun.color.a below
	UniformCloudRendererObject& cloudrenderer = skySystem.getCloudRenderer();

	// Pass a uniform value in sun.color.a indicating which of the 16 pixels should be updated.
	// Yes, this should have its own uniform but you would have to pad to sizeof(vec4) and we are not even using 
	// this channel already. Will probably change later.
	sun.color.a = ((int)sun.color.a + 1) % 16; // update every 16th pixel

	//update cloud renderer Parameters for UI Panel
	cloudrenderer.coverage_rate = rendererSystem.GetFloatParams("coverage_rate");
	cloudrenderer.erosion_rate = rendererSystem.GetFloatParams("erosion_rate");
	cloudrenderer.extinction = rendererSystem.GetFloatParams("extinction");
	cloudrenderer.tempfloat = rendererSystem.GetFloatParams("tempfloat");
	cloudrenderer.cloudinfo1 = rendererSystem.GetVectorParams("cloudinfo1");
	cloudrenderer.cloudinfo2 = rendererSystem.GetVectorParams("cloudinfo2");
	cloudrenderer.cloudinfo3 = rendererSystem.GetVectorParams("cloudinfo3");
	cloudrenderer.cloudinfo4 = rendererSystem.GetVectorParams("cloudinfo4");
	cloudrenderer.cloudinfo5 = rendererSystem.GetVectorParams("cloudinfo5");
	cloudrenderer.wind_direction = rendererSystem.GetVectorParams("cirrusWind_direction");
	cloudrenderer.tempVector = rendererSystem.GetVectorParams("tempVector");
	glm::vec4 wind = rendererSystem.GetVectorParams("wind_direction");
	sky.wind = glm::vec4(wind.x, wind.y, wind.z, sky.wind.w);
	//sun.intensity = sun.intensity*rendererSystem.GetFloatParams("sun_intensity");

	computeShader->updateUniformBuffers(uco, ucoPrev, sky, sun, cloudrenderer);
	reprojectShader->updateUniformBuffers(uco, ucoPrev, sky, sun);
	meshShader->updateUniformBuffers(uco, umo, sun, sky);
	godRayShader->updateUniformBuffers(uco, sun);
	radialBlurShader->updateUniformBuffers(uco, sun);

	std::stringstream ss;
	ss << 1.0 / deltaTime;
	glfwSetWindowTitle(window, ss.str().c_str());
}

uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties, VkPhysicalDevice physicalDevice) {
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
		if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
			return i;
		}
	}
	throw std::runtime_error("failed to find suitable memory type!");
}

// copy the contents from one buffer to another
void VulkanApplication::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
	VkCommandBuffer commandBuffer = beginSingleTimeCommands();

	VkBufferCopy copyRegion = {};
	copyRegion.size = size;
	vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

	endSingleTimeCommands(commandBuffer);
}

VkImageView VulkanApplication::createImageView(VkImage image, VkFormat format) {
	VkImageViewCreateInfo viewInfo = {};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = format;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; //VK_IMAGE_USAGE_STORAGE_BIT
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	VkImageView imageView;
	if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
		throw std::runtime_error("failed to create texture image view!");
	}

	return imageView;
}

/// --- Vulkan Setup Functions

void VulkanApplication::createInstance() {

#ifdef _DEBUG
	if (!checkValidationLayerSupport()) {
		throw std::runtime_error("validation layers not supported");
	}
#endif

	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Sky Engine";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "No Engine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_0;

	VkInstanceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;

	// check GLFW extensions
	unsigned int glfwExtensionCount = 0;
	const char** glfwExtensions;
	glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
	createInfo.enabledExtensionCount = glfwExtensionCount;
	createInfo.ppEnabledExtensionNames = glfwExtensions;

	auto allextensions = getRequiredExtensions();
	createInfo.enabledExtensionCount = static_cast<uint32_t>(allextensions.size());
	createInfo.ppEnabledExtensionNames = allextensions.data();

	// Validation Layer enabling
# if _DEBUG
	createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
	createInfo.ppEnabledLayerNames = validationLayers.data();
# else
	createInfo.enabledLayerCount = 0;
# endif


	//VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);

	if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
		throw std::runtime_error("failed to create instance!");
	}
}

// If debugging is enabled, ensure that Vulkan is able to use validation layers.
bool VulkanApplication::checkValidationLayerSupport() {

#ifdef _DEBUG

	uint32_t layerCount;
	vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

	std::vector<VkLayerProperties> availableLayers(layerCount);
	vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

	for (const char* layerName : validationLayers) {
		bool layerFound = false;

		for (const auto& layerProperties : availableLayers) {
			if (strcmp(layerName, layerProperties.layerName) == 0) {
				layerFound = true;
				break;
			}
		}

		if (!layerFound) {
			return false;
		}
	}

	return true;
#else
	return false;
#endif // _DEBUG

}

std::vector<const char*> VulkanApplication::getRequiredExtensions() {
	std::vector<const char*> extensions;

	unsigned int glfwExtensionCount = 0;
	const char** glfwExtensions;
	glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

	for (unsigned int i = 0; i < glfwExtensionCount; i++) {
		extensions.push_back(glfwExtensions[i]);
	}

#ifdef _DEBUG
	extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
	
#endif
	return extensions;
}

void VulkanApplication::setupDebugCallback() {
#ifdef _DEBUG
	VkDebugReportCallbackCreateInfoEXT createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
	createInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
	createInfo.pfnCallback = debugCallback;

	if (CreateDebugReportCallbackEXT(instance, &createInfo, nullptr, &callback) != VK_SUCCESS) {
		throw std::runtime_error("failed to set up debug callback!");
	}
#else 
	return;
#endif
}

bool VulkanApplication::isDeviceSuitable(VkPhysicalDevice device) {
	/* TODO what is this lul
		VkPhysicalDeviceProperties deviceProperties;
		vkGetPhysicalDeviceProperties(device, &deviceProperties);

		VkPhysicalDeviceFeatures deviceFeatures;
		vkGetPhysicalDeviceFeatures(device, &deviceFeatures);
		*/
		// Add necessary features here, e.g.
		//return deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
		//deviceFeatures.geometryShader;

	QueueFamilyIndices indices = findQueueFamilies(device);

	bool extensionsSupported = checkDeviceExtensionSupport(device);

	bool swapChainAdequate = false;
	if (extensionsSupported) {
		SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
		swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
	}

	return indices.isComplete() && extensionsSupported && swapChainAdequate;
}

// Find the best GPU to run Vulkan on. Fail if nothing is suitable.
void VulkanApplication::pickPhysicalDevice() {
	uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
	if (deviceCount == 0) {
		throw std::runtime_error("failed to find GPUs with Vulkan support!");
	}
	std::vector<VkPhysicalDevice> devices(deviceCount);
	vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

	for (const auto& device : devices) {
		if (isDeviceSuitable(device)) {
			physicalDevice = device;
			break;
		}
	}

	if (physicalDevice == VK_NULL_HANDLE) {
		throw std::runtime_error("failed to find a suitable GPU!");
	}
}

// Check what command queues the GPU is capable of handling.
QueueFamilyIndices VulkanApplication::findQueueFamilies(VkPhysicalDevice device) {
	QueueFamilyIndices indices;

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

	int i = 0;
	for (const auto& queueFamily : queueFamilies) {
		if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			if (queueFamily.timestampValidBits <= 0)
			{
				throw std::string("DeviceProperty: timestamp is not supported ");
			}
			indices.graphicsFamily = i;
		}

		//TODO: is this correct
		if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) {
			indices.computeFamily = i;
		}

		VkBool32 presentSupport = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

		if (queueFamily.queueCount > 0 && presentSupport) {
			indices.presentFamily = i;
		}

		if (indices.isComplete()) {
			break;
		}

		i++;
	}

	return indices;
}

// After picking the GPU, make a logical device representation.
void VulkanApplication::createLogicalDevice() {

	QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
	std::set<int> uniqueQueueFamilies = { indices.graphicsFamily, indices.presentFamily };

	float queuePriority = 1.0f;
	for (int queueFamily : uniqueQueueFamilies) {
		VkDeviceQueueCreateInfo queueCreateInfo = {};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = queueFamily;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &queuePriority;
		queueCreateInfos.push_back(queueCreateInfo);
	}

	//在逻辑设备创建过程中，必须填写一个结构并将其添加到链中pNext以启用主机查询重置功能
	VkPhysicalDeviceHostQueryResetFeaturesEXT resetFeatures;
	resetFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES_EXT;
	resetFeatures.pNext = nullptr;
	resetFeatures.hostQueryReset = VK_TRUE;

	// TODO : modify with specific features
	VkPhysicalDeviceFeatures deviceFeatures = {};
	deviceFeatures.samplerAnisotropy = VK_TRUE;

	VkDeviceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.pQueueCreateInfos = queueCreateInfos.data();
	createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
	createInfo.pEnabledFeatures = &deviceFeatures;
	createInfo.pNext = &resetFeatures;//enable requestreset feature

	createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
	createInfo.ppEnabledExtensionNames = deviceExtensions.data();

#ifdef _DEBUG //DEBUG_VALIDATION

	createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
	createInfo.ppEnabledLayerNames = validationLayers.data();

#else

	createInfo.enabledLayerCount = 0;
#endif

	if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
		throw std::runtime_error("failed to create logical device!");
	}

	// TODO : multiple queues
	vkGetDeviceQueue(device, indices.graphicsFamily, 0, &graphicsQueue);
	vkGetDeviceQueue(device, indices.computeFamily, 0, &computeQueue);
	vkGetDeviceQueue(device, indices.presentFamily, 0, &presentQueue);
}

// Make a surface for Vulkan to draw on. GLFW handles this. (Platform-dependent)
void VulkanApplication::createSurface() {
	if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
		throw std::runtime_error("failed to create window surface!");
	}
}

bool extensionPresent = false;

bool VulkanApplication::checkDeviceExtensionSupport(VkPhysicalDevice device) {
	uint32_t extensionCount;
	vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

	std::vector<VkExtensionProperties> availableExtensions(extensionCount);
	vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

	std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

	for (const auto& extension : availableExtensions) {
		requiredExtensions.erase(extension.extensionName);

		//if (strcmp(extension.extensionName, VK_EXT_DEBUG_MARKER_EXTENSION_NAME) == 0) {
		//	extensionPresent = true;
		//	std::cout << "Warning: " << VK_EXT_DEBUG_MARKER_EXTENSION_NAME << " avaliable.";
		//	break;
		//}
	}

	return requiredExtensions.empty();
}

/// --- Swap Chain Functions

// TODO: move ASAP to a shader class
/// DELETE
VkShaderModule createShaderModule(const std::vector<char>& code, VkDevice device) {
	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = code.size();
	createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

	VkShaderModule shaderModule;
	if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
		throw std::runtime_error("failed to create shader module!");
	}

	return shaderModule;
}

// TODO: get this out
VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features, VkPhysicalDevice physicalDevice) {
	for (VkFormat format : candidates) {
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

		if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
			return format;
		}
		else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
			return format;
		}
	}

	throw std::runtime_error("failed to find supported format!");
}

VkFormat findDepthFormat(VkPhysicalDevice physicalDevice) {
	return findSupportedFormat(
		{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, physicalDevice
	);
}
void VulkanApplication::createRenderPass() {
	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = swapChainImageFormat;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // color and depth buffer
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference colorAttachmentRef = {};
	colorAttachmentRef.attachment = 0; // shader does layout(location = 0) for color!
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentDescription depthAttachment = {};
	depthAttachment.format = findDepthFormat(physicalDevice); // depthTexture->getFormat(); << made out of order
	depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthAttachmentRef = {};
	depthAttachmentRef.attachment = 1;
	depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachmentRef;
	subpass.pDepthStencilAttachment = &depthAttachmentRef;

	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = 1;
	renderPassInfo.pDependencies = &dependency;

	if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
		throw std::runtime_error("failed to create render pass!");
	}
}

void VulkanApplication::createSemaphores() {
	VkSemaphoreCreateInfo semaphoreInfo = {};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphore) != VK_SUCCESS ||
		vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphore) != VK_SUCCESS) {

		throw std::runtime_error("failed to create semaphores!");
	}


}

void VulkanApplication::createCommandPool() {
	QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);

	VkCommandPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;
	poolInfo.flags = 0; // for recording once and executing many times. 
	// otherwise, use VK_COMMAND_POOL_CREATE_TRANSIENT_BIT -or- VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT

	if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
		throw std::runtime_error("failed to create command pool!");
	}

	// Compute command pool
	VkCommandPoolCreateInfo computePoolInfo = {};
	computePoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	computePoolInfo.queueFamilyIndex = queueFamilyIndices.computeFamily; //TODO: need compute index or whatever
	computePoolInfo.flags = 0;

	if (vkCreateCommandPool(device, &computePoolInfo, nullptr, &computeCommandPool) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create command pool");
	}
}

//void VulkanApplication::CreateQueryPool()
//{
//	VkQueryPoolCreateInfo createInfo{};
//	createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
//	createInfo.pNext = nullptr; // Optional
//	createInfo.flags = 0; // Reserved for future use, must be 0!
//
//	createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
//	createInfo.queryCount = offscreenPass.commandBuffers.size() * 2; // REVIEW
//
//	VkResult result = vkCreateQueryPool(device, &createInfo, nullptr, &mTimeQueryPool);
//	if (result != VK_SUCCESS)
//	{
//		throw std::runtime_error("Failed to create time query pool!");
//	}
//}

//void VulkanApplication::FetchRenderTimeResults(uint32_t swapchainIndex)
//{
//	uint64_t buffer[2];
//
//	VkResult result = vkGetQueryPoolResults(device, mTimeQueryPool, swapchainIndex * 2, 2, sizeof(uint64_t) * 2, buffer, sizeof(uint64_t),
//		VK_QUERY_RESULT_64_BIT);
//	if (result == VK_NOT_READY)
//	{
//		return;
//	}
//	else if (result == VK_SUCCESS)
//	{
//		mTimeQueryResults[swapchainIndex] = buffer[1] - buffer[0];
//	}
//	else
//	{
//		throw std::runtime_error("Failed to receive query results!");
//	}
//
//	// Queries must be reset after each individual use.
//	// resetquery is supported since Vulkan 1.2
//	//vkResetQueryPoolEXT(device, mTimeQueryPool, swapchainIndex * 2, 2);
//}

VkCommandBuffer VulkanApplication::beginSingleTimeCommands() {
	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandPool = commandPool;
	allocInfo.commandBufferCount = 1;

	VkCommandBuffer commandBuffer;
	vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	vkBeginCommandBuffer(commandBuffer, &beginInfo);

	return commandBuffer;
}

void VulkanApplication::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
	vkEndCommandBuffer(commandBuffer);

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
	vkQueueWaitIdle(graphicsQueue);

	vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

// This function renders everything that is offscreen. The PostProcessCommandBuffer actually renders to the screen.
void VulkanApplication::createCommandBuffers() {

	if (offscreenPass.commandBuffers.size() == 0)
	{
		offscreenPass.commandBuffers.resize(2);
		VkCommandBufferAllocateInfo commandBufferAllocateInfo{};
		commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		commandBufferAllocateInfo.commandPool = commandPool;
		commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		commandBufferAllocateInfo.commandBufferCount = 1;

		if (vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, &offscreenPass.commandBuffers[0]) != VK_SUCCESS) {
			throw std::runtime_error("failed to allocate offscreen command buffer!");
		}
		if (vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, &offscreenPass.commandBuffers[1]) != VK_SUCCESS) {
			throw std::runtime_error("failed to allocate offscreen command buffer!");
		}
	}

	if (offscreenPass.semaphore == VK_NULL_HANDLE)
	{
		VkSemaphoreCreateInfo semaphoreCreateInfo{};
		semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		if (vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &offscreenPass.semaphore) != VK_SUCCESS) {
			throw std::runtime_error("failed to allocate offscreen semaphore!");
		}
	}

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
	beginInfo.pInheritanceInfo = nullptr; // Optional

	for (int i = 0; i < offscreenPass.commandBuffers.size(); i++) {
		vkBeginCommandBuffer(offscreenPass.commandBuffers[i], &beginInfo);

		std::array<VkClearValue, 2> clearValues = {};
		clearValues[0].color = { 0.0f, 0.0f, 0.0f, 1.0f };
		clearValues[1].depthStencil = { 1.0f, 0 };

		// Actual render pass creation
		VkRenderPassBeginInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = offscreenPass.renderPass;
		renderPassInfo.framebuffer = offscreenPass.framebuffers[0].framebuffer;
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent = swapChainExtent;
		VkClearValue clearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
		renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassInfo.pClearValues = clearValues.data();

		//// time stamp query supported since vulkan1.2
		//vkCmdWriteTimestamp(offscreenPass.commandBuffers[i], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, mTimeQueryPool, i * 2);

		// Render pass recording
		vkCmdBeginRenderPass(offscreenPass.commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		// Draw Background
		backgroundShader->bindShader(offscreenPass.commandBuffers[i]);
		backgroundGeometry->enqueueDrawCommands(offscreenPass.commandBuffers[i]);

		vkCmdEndRenderPass(offscreenPass.commandBuffers[i]);

		// Use the next framebuffer in the offscreen pass
		renderPassInfo.framebuffer = offscreenPass.framebuffers[1].framebuffer;

		// God rays and mesh drawing

		vkCmdBeginRenderPass(offscreenPass.commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		godRayShader->bindShader(offscreenPass.commandBuffers[i]);
		backgroundGeometry->enqueueDrawCommands(offscreenPass.commandBuffers[i]);

		vkCmdEndRenderPass(offscreenPass.commandBuffers[i]);

		// Use the next framebuffer in the offscreen pass
		renderPassInfo.framebuffer = offscreenPass.framebuffers[2].framebuffer;

		// Radial Blur
		vkCmdBeginRenderPass(offscreenPass.commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		radialBlurShader->bindShader(offscreenPass.commandBuffers[i]);
		backgroundGeometry->enqueueDrawCommands(offscreenPass.commandBuffers[i]);

		// Draw Scene
		meshShader->bindShader(offscreenPass.commandBuffers[i]);
		sceneGeometry->enqueueDrawCommands(offscreenPass.commandBuffers[i]);

		vkCmdEndRenderPass(offscreenPass.commandBuffers[i]);

		//// time stamp query
		//vkCmdWriteTimestamp(offscreenPass.commandBuffers[i], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, mTimeQueryPool, i * 2 + 1);

		if (vkEndCommandBuffer(offscreenPass.commandBuffers[i]) != VK_SUCCESS) {
			throw std::runtime_error("failed to record offscreen command buffer!");
		}
	}

}

// Run the final post process that renders to the screen
void VulkanApplication::createPostProcessCommandBuffer() {
	commandBuffers.resize(swapChainFramebuffers.size());
	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

	if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
		throw std::runtime_error("failed to allocate command buffers!");
	}

	for (size_t i = 0; i < commandBuffers.size(); i++) {
		VkCommandBufferBeginInfo beginInfo = {};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
		beginInfo.pInheritanceInfo = nullptr; // Optional

		vkBeginCommandBuffer(commandBuffers[i], &beginInfo);

		std::array<VkClearValue, 2> clearValues = {};
		clearValues[0].color = { 0.0f, 0.0f, 0.0f, 1.0f };
		clearValues[1].depthStencil = { 1.0f, 0 };

		// Actual render pass creation
		VkRenderPassBeginInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = renderPass;
		renderPassInfo.framebuffer = swapChainFramebuffers[i];
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent = swapChainExtent;
		VkClearValue clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
		renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassInfo.pClearValues = clearValues.data();

		// Render pass recording
		vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		toneMapShader->bindShader(commandBuffers[i]);
		backgroundGeometry->enqueueDrawCommands(commandBuffers[i]);

		vkCmdEndRenderPass(commandBuffers[i]);

		if (vkEndCommandBuffer(commandBuffers[i]) != VK_SUCCESS) {
			throw std::runtime_error("failed to record command buffer!");
		}
	}
}

void VulkanApplication::createComputeCommandBuffer() {
	computeCommandBuffers.resize(2); // need to swap back and forth for background

	// Specify the command pool and number of buffers to allocate
	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = computeCommandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = (uint32_t)computeCommandBuffers.size();

	if (vkAllocateCommandBuffers(device, &allocInfo, computeCommandBuffers.data()) != VK_SUCCESS) {
		throw std::runtime_error("Failed to allocate command buffers");
	}

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
	beginInfo.pInheritanceInfo = nullptr;

	// need 2 buffers to ping-pong draw targets
	for (int i = 0; i < 2; i++) {
		// Begin recording
		if (vkBeginCommandBuffer(computeCommandBuffers[i], &beginInfo) != VK_SUCCESS) {
			throw std::runtime_error("Failed to begin recording compute command buffer");
		}

		reprojectShader->bindShader(computeCommandBuffers[i]);

		const glm::ivec2 texDimsFull(swapChainExtent.width, swapChainExtent.height);
		vkCmdDispatch(computeCommandBuffers[i],
			static_cast<uint32_t>((texDimsFull.x + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE),
			static_cast<uint32_t>((texDimsFull.y + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE),
			1);

		// compute shader will switch descriptor set binding inside this function
		computeShader->bindShader(computeCommandBuffers[i]);

		// TODO: dispatch according to the number of pixels, do in a 2d manner? see the raytracing example
		// first TODO: launch this compute shader for the triangle being rendered
		//const int IMAGE_SIZE = 32;
		const glm::ivec2 texDims(swapChainExtent.width / 4, swapChainExtent.height / 4);
		vkCmdDispatch(computeCommandBuffers[i],
			static_cast<uint32_t>((texDims.x + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE),
			static_cast<uint32_t>((texDims.y + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE),
			1);

		// End recording
		if (vkEndCommandBuffer(computeCommandBuffers[i]) != VK_SUCCESS) {
			throw std::runtime_error("Failed to record compute command buffer");
		}
	}
}

void VulkanApplication::createFramebuffers() {
	swapChainFramebuffers.resize(swapChainImageViews.size());
	// iterate through all image views and create frame buffers from them
	for (size_t i = 0; i < swapChainImageViews.size(); i++) {
		std::array<VkImageView, 2> attachments = {
			swapChainImageViews[i],
			depthTexture->textureImageView
		};

		VkFramebufferCreateInfo framebufferInfo = {};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = renderPass;
		framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		framebufferInfo.pAttachments = attachments.data();
		framebufferInfo.width = swapChainExtent.width;
		framebufferInfo.height = swapChainExtent.height;
		framebufferInfo.layers = 1;

		if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS) {
			throw std::runtime_error("failed to create framebuffer!");
		}
	}
}

// Needs to be called once for each post process effect
// Expects a framebuffer, color and depth format to sample
void VulkanApplication::createOffscreenFramebuffer(FrameBuffer* framebuffer, VkFormat colorFormat, VkFormat depthFormat)
{
	// Color attachment
	VkImageCreateInfo image{};
	image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image.imageType = VK_IMAGE_TYPE_2D;
	image.format = colorFormat;
	image.extent.width = WIDTH;
	image.extent.height = HEIGHT;
	image.extent.depth = 1;
	image.mipLevels = 1;
	image.arrayLayers = 1;
	image.samples = VK_SAMPLE_COUNT_1_BIT;
	image.tiling = VK_IMAGE_TILING_OPTIMAL;
	// We will sample directly from the color attachment
	image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

	VkMemoryAllocateInfo memAlloc{};
	memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	VkMemoryRequirements memReqs;

	VkImageViewCreateInfo colorImageView{};
	colorImageView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	colorImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
	colorImageView.format = colorFormat;
	colorImageView.flags = 0;
	colorImageView.subresourceRange = {};
	colorImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	colorImageView.subresourceRange.baseMipLevel = 0;
	colorImageView.subresourceRange.levelCount = 1;
	colorImageView.subresourceRange.baseArrayLayer = 0;
	colorImageView.subresourceRange.layerCount = 1;

	if (vkCreateImage(device, &image, nullptr, &framebuffer->color.image) != VK_SUCCESS) {
		throw std::runtime_error("failed to create image!");
	}

	vkGetImageMemoryRequirements(device, framebuffer->color.image, &memReqs);
	memAlloc.allocationSize = memReqs.size;
	memAlloc.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, physicalDevice);
	if (vkAllocateMemory(device, &memAlloc, nullptr, &framebuffer->color.mem) != VK_SUCCESS) {
		throw std::runtime_error("failed to allocate memory!");
	}

	if (vkBindImageMemory(device, framebuffer->color.image, framebuffer->color.mem, 0) != VK_SUCCESS) {
		throw std::runtime_error("failed to bind image memory!");
	}

	colorImageView.image = framebuffer->color.image;
	if (vkCreateImageView(device, &colorImageView, nullptr, &framebuffer->color.view) != VK_SUCCESS) {
		throw std::runtime_error("failed to create image view!");
	}

	// Depth stencil attachment
	image.format = depthFormat;
	image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	VkImageViewCreateInfo depthStencilView{};
	depthStencilView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	depthStencilView.viewType = VK_IMAGE_VIEW_TYPE_2D;
	depthStencilView.format = depthFormat;
	depthStencilView.flags = 0;
	depthStencilView.subresourceRange = {};
	depthStencilView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;// | VK_IMAGE_ASPECT_STENCIL_BIT;
	depthStencilView.subresourceRange.baseMipLevel = 0;
	depthStencilView.subresourceRange.levelCount = 1;
	depthStencilView.subresourceRange.baseArrayLayer = 0;
	depthStencilView.subresourceRange.layerCount = 1;

	if (vkCreateImage(device, &image, nullptr, &framebuffer->depth.image) != VK_SUCCESS) {
		throw std::runtime_error("failed to create image!");
	}
	vkGetImageMemoryRequirements(device, framebuffer->depth.image, &memReqs);
	memAlloc.allocationSize = memReqs.size;
	memAlloc.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, physicalDevice);
	if (vkAllocateMemory(device, &memAlloc, nullptr, &framebuffer->depth.mem) != VK_SUCCESS) {
		throw std::runtime_error("failed to allocate memory!");
	}

	if (vkBindImageMemory(device, framebuffer->depth.image, framebuffer->depth.mem, 0) != VK_SUCCESS) {
		throw std::runtime_error("failed to bind image!");
	}

	depthStencilView.image = framebuffer->depth.image;
	if (vkCreateImageView(device, &depthStencilView, nullptr, &framebuffer->depth.view) != VK_SUCCESS) {
		throw std::runtime_error("failed to create image view!");
	}

	VkImageView attachments[2];
	attachments[0] = framebuffer->color.view;
	attachments[1] = framebuffer->depth.view;

	VkFramebufferCreateInfo fbufCreateInfo{};
	fbufCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fbufCreateInfo.renderPass = offscreenPass.renderPass;
	fbufCreateInfo.attachmentCount = 2;
	fbufCreateInfo.pAttachments = attachments;
	fbufCreateInfo.width = WIDTH;
	fbufCreateInfo.height = HEIGHT;
	fbufCreateInfo.layers = 1;

	if (vkCreateFramebuffer(device, &fbufCreateInfo, nullptr, &framebuffer->framebuffer) != VK_SUCCESS) {
		throw std::runtime_error("failed to create framebuffer!");
	}

	// Fill a descriptor for later use in a descriptor set 
	framebuffer->descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	framebuffer->descriptor.imageView = framebuffer->color.view;
	framebuffer->descriptor.sampler = offscreenPass.sampler;
}

void VulkanApplication::setupOffscreenPass() {
	offscreenPass.width = WIDTH;
	offscreenPass.height = HEIGHT;

	// Find a suitable depth format
	VkFormat fbDepthFormat = findDepthFormat(physicalDevice);

	// Create a separate render pass for the offscreen rendering as it may differ from the one used for scene rendering
	std::array<VkAttachmentDescription, 2> attachmentDescriptions = {};

	// Color attachment
	attachmentDescriptions[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
	attachmentDescriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachmentDescriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachmentDescriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachmentDescriptions[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachmentDescriptions[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachmentDescriptions[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachmentDescriptions[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	// Depth attachment
	attachmentDescriptions[1].format = fbDepthFormat;
	attachmentDescriptions[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachmentDescriptions[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachmentDescriptions[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachmentDescriptions[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachmentDescriptions[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachmentDescriptions[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachmentDescriptions[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	VkAttachmentReference depthReference = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

	VkSubpassDescription subpassDescription = {};
	subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDescription.colorAttachmentCount = 1;
	subpassDescription.pColorAttachments = &colorReference;
	subpassDescription.pDepthStencilAttachment = &depthReference;

	// Use subpass dependencies for layout transitions
	std::array<VkSubpassDependency, 2> dependencies;

	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	// Create the actual renderpass
	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachmentDescriptions.size());
	renderPassInfo.pAttachments = attachmentDescriptions.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &offscreenPass.renderPass) != VK_SUCCESS) {
		throw std::runtime_error("failed to create render pass!");
	}

	// Create sampler to sample from the color attachments
	VkSamplerCreateInfo sampler{};
	sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler.magFilter = VK_FILTER_LINEAR;
	sampler.minFilter = VK_FILTER_LINEAR;
	sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.addressModeV = sampler.addressModeU;
	sampler.addressModeW = sampler.addressModeU;
	sampler.mipLodBias = 0.0f;
	sampler.maxAnisotropy = 1.0f;
	sampler.minLod = 0.0f;
	sampler.maxLod = 1.0f;
	sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	if (vkCreateSampler(device, &sampler, nullptr, &offscreenPass.sampler) != VK_SUCCESS) {
		throw std::runtime_error("failed to create sampler!");
	}

	// Create offscreen frame buffers - note the image format, they are HDR
	createOffscreenFramebuffer(&offscreenPass.framebuffers[0], VK_FORMAT_R32G32B32A32_SFLOAT, fbDepthFormat);
	createOffscreenFramebuffer(&offscreenPass.framebuffers[1], VK_FORMAT_R32G32B32A32_SFLOAT, fbDepthFormat);
	createOffscreenFramebuffer(&offscreenPass.framebuffers[2], VK_FORMAT_R32G32B32A32_SFLOAT, fbDepthFormat);
}

void VulkanApplication::createSwapChain() {
	SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);

	VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
	VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
	VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

	swapChainImageFormat = surfaceFormat.format;
	swapChainExtent = extent;

	// queue length. 0 means no limit aside from memory
	uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
	if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
		imageCount = swapChainSupport.capabilities.maxImageCount;
	}

	VkSwapchainCreateInfoKHR createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	createInfo.surface = surface;
	createInfo.minImageCount = imageCount;
	createInfo.imageFormat = surfaceFormat.format;
	createInfo.imageColorSpace = surfaceFormat.colorSpace;
	createInfo.imageExtent = extent;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
	uint32_t queueFamilyIndices[] = { (uint32_t)indices.graphicsFamily, (uint32_t)indices.presentFamily };

	if (indices.graphicsFamily != indices.presentFamily) {
		createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		createInfo.queueFamilyIndexCount = 2;
		createInfo.pQueueFamilyIndices = queueFamilyIndices;
	}
	else {
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.queueFamilyIndexCount = 0;
		createInfo.pQueueFamilyIndices = nullptr;
	}

	createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
	createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	createInfo.oldSwapchain = VK_NULL_HANDLE; // only one swap chain for now
	createInfo.presentMode = presentMode;
	createInfo.clipped = VK_TRUE;

	if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) != VK_SUCCESS) {
		throw std::runtime_error("failed to create swap chain!");
	}

	// create the handles from the swap chain to images
	vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
	swapChainImages.resize(imageCount);
	vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());
}

// Same as cleanup(), but all things related to swapchain
void VulkanApplication::cleanupSwapChain() {
	for (size_t i = 0; i < swapChainFramebuffers.size(); i++) {
		vkDestroyFramebuffer(device, swapChainFramebuffers[i], nullptr);
	}

	vkFreeCommandBuffers(device, commandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());

	//vkDestroyPipeline(device, graphicsPipeline, nullptr);
	//vkDestroyPipelineLayout(device, graphicsPipelineLayout, nullptr);
	vkDestroyRenderPass(device, renderPass, nullptr);

	for (size_t i = 0; i < swapChainImageViews.size(); i++) {
		vkDestroyImageView(device, swapChainImageViews[i], nullptr);
	}

	vkDestroySwapchainKHR(device, swapChain, nullptr);
}

// TODO
void VulkanApplication::recreateSwapChain() {
	vkDeviceWaitIdle(device);

	cleanupSwapChain();

	createSwapChain();
	createImageViews();
	createRenderPass();
	//createGraphicsPipeline();
	createFramebuffers();
	createCommandBuffers();
}

void VulkanApplication::createImageViews() {
	swapChainImageViews.resize(swapChainImages.size());

	for (uint32_t i = 0; i < swapChainImages.size(); i++) {
		swapChainImageViews[i] = createImageView(swapChainImages[i], swapChainImageFormat);
	}
}

// Populate the details of swap chain capabilities
SwapChainSupportDetails VulkanApplication::querySwapChainSupport(VkPhysicalDevice device) {
	SwapChainSupportDetails details;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

	uint32_t formatCount;
	vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

	if (formatCount != 0) {
		details.formats.resize(formatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
	}

	uint32_t presentModeCount;
	vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

	if (presentModeCount != 0) {
		details.presentModes.resize(presentModeCount);
		vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
	}


	return details;
}

// Determine the format / color space of the swap chain, e.g. r8g8b8a8
VkSurfaceFormatKHR VulkanApplication::chooseSwapSurfaceFormat(const std::vector <VkSurfaceFormatKHR>& availableFormats) {
	// default: no preferred format
	if (availableFormats.size() == 1 && availableFormats[0].format == VK_FORMAT_UNDEFINED) {
		return{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
	}

	for (const auto& availableFormat : availableFormats) {
		if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			return availableFormat;
		}
	}

	return availableFormats[0]; // probably shouldn't happen
}

// Determine the way the swap chain will handle submitted images
VkPresentModeKHR VulkanApplication::chooseSwapPresentMode(const std::vector<VkPresentModeKHR> availablePresentModes) {
	// Types of modes:
	// IMMEDIATE: submitted by app goes to screen ASAP
	// FIFO: queue of images, analagous to vsync
	// FIFO_RELAXED: queue but does ASAP transfer if queue is empty
	// MAILBOX: queue but overwrites if full

	// MAILBOX is ideal but not guaranteed to be supported
	VkPresentModeKHR bestMode = VK_PRESENT_MODE_FIFO_KHR;

	for (const auto& availablePresentMode : availablePresentModes) {
		if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
			return availablePresentMode;
		}
		//		else if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
		//			bestMode = availablePresentMode;
		//		}
	}

	return bestMode;
}

// Get the resolution of the swap chain
VkExtent2D VulkanApplication::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
	if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
		return capabilities.currentExtent;
	}
	else {
		int width, height;
		glfwGetWindowSize(window, &width, &height);

		VkExtent2D actualExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };

		actualExtent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, actualExtent.width));
		actualExtent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, actualExtent.height));

		return actualExtent;
	}
}