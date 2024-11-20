/*
* Vulkan Example - Drawing multiple animated gears (emulating the look of glxgears)
*
* All gears are using single index, vertex and uniform buffers to show the Vulkan best practices of keeping the no. of buffer/memory allocations to a mimimum
* We use index offsets and instance indices to offset into the buffers at draw time for each gear
*
* Copyright (C) 2016-2023 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "vulkanexamplebase.h"

#if defined(_WIN32)
const std::string default_root_folder = "C:\\smpl_model\\";
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
const std::string default_root_folder = "/data/local/tmp";
#endif

std::string root_folder = default_root_folder;
std::string config_filename = root_folder + "config.json";
std::string index_filename = root_folder + "index.bin";
std::string vertex_filename = root_folder + "vertices.bin";
float human_color[3] = { 200.f / 255.0f, 200.f / 255.0f, 200.f / 255.0f };

using json = nlohmann::json;

glm::vec3 ComputeFaceNormal(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2) {
	glm::vec3 edge1 = v1 - v0;
	glm::vec3 edge2 = v2 - v0;
	glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));
	return normal;
}

void ComputeVertexNormals(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& faces, std::vector<glm::vec3>& normals) {
	normals.resize(vertices.size(), glm::vec3(0.0f));
	std::vector<int> count(vertices.size(), 0);

	for (auto i{ 0 }; i < faces.size(); i += 3) {
		auto index0 = faces[i], index1 = faces[i + 1], index2 = faces[i + 2];
		glm::vec3 normal = ComputeFaceNormal(vertices[index0], vertices[index1], vertices[index2]);
		normals[index0] += normal;
		normals[index1] += normal;
		normals[index2] += normal;
		count[index0]++;
		count[index1]++;
		count[index2]++;
	}


	for (size_t i = 0; i < normals.size(); ++i) {
		if (count[i] > 0) {
			normals[i] = glm::normalize(normals[i] / static_cast<float>(count[i]));
		}
	}
}

class SMPLModel {

public:
	// The vertex layout for the model
	struct Vertex {
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec3 color;
	};

	// A primitive contains the data for a single draw call
	struct Primitive {
		uint32_t firstIndex;
		uint32_t indexCount;
	};

	// Contains the node's (optional) geometry and can be made up of an arbitrary number of primitives. 
	struct Mesh {
		std::vector<Primitive> primitives;
	};

	SMPLModel(vks::VulkanDevice* const vulkan_device, VkQueue queue) : vulkan_device_(vulkan_device), queue_(queue)
	{
		GetVertexInfo(); // get vertex count and size
		CreateBuffers(); // create buffer based on size
		UpdateIndexBuffer(); // index buffer is fixed
		vertex_binary_file_.open(vertex_filename, std::ios::binary); // open the file to prepare to read
	}

	~SMPLModel()
	{
		// free buffer
		vertex_buffer_.destroy();
		vertex_staging_buffer_.destroy();
		index_buffer_.destroy();
		index_staging_buffer_.destroy();
		// close file
		if (vertex_binary_file_.is_open())
			vertex_binary_file_.close();
	}

	void LoadVertexData(int current_frame)
	{
		std::vector<Vertex> model_vertexs(vertex_count_);
		std::vector<glm::vec3> vertexs(vertex_count_), normals(vertex_count_);
		size_t vertex_binary_size = vertex_count_ * sizeof(glm::vec3);

		if (vertex_binary_file_.is_open())
		{
			vertex_binary_file_.seekg(current_frame * vertex_binary_size, std::ios::beg);
			vertex_binary_file_.read(reinterpret_cast<char*>(vertexs.data()), vertex_binary_size);
			ComputeVertexNormals(vertexs, indexs_, normals);
			for (auto i{ 0 }; i < vertexs.size(); i++) {
				model_vertexs[i].position = { vertexs[i].x , vertexs[i].y , vertexs[i].z };
				model_vertexs[i].normal = normals[i];
				model_vertexs[i].color = { human_color[0], human_color[1], human_color[2] };
			}

			UpdateVertexBuffer(model_vertexs);
		}
	}

	void Draw(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout)
	{
		// All vertices and indices are stored in single buffers, so we only need to bind once
		VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertex_buffer_.buffer, offsets);
		vkCmdBindIndexBuffer(commandBuffer, index_buffer_.buffer, 0, VK_INDEX_TYPE_UINT32);
		// Render model
		uint32_t index = 0;
		for (auto& primitive : mesh_.primitives) {
			vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &matrix_);
			vkCmdDrawIndexed(commandBuffer, primitive.indexCount, 1, primitive.firstIndex, 0, index);
		}
	}


private:
	void GetVertexInfo() {
		std::fstream file(config_filename);
		json j = json::parse(file);
		j["vertex_count"].get_to(vertex_count_);
		j["index_count"].get_to(index_count_);
		index_buffer_size_ = index_count_ * sizeof(uint32_t);
		vertex_buffer_size_ = vertex_count_ * sizeof(Vertex);
		file.close();
		indexs_.resize(index_count_);

		// mesh data
		mesh_.primitives.resize(1); // TODO: fix to 1 now
		mesh_.primitives[0].firstIndex = 0;
		mesh_.primitives[0].indexCount = index_count_;
	}

	void CreateBuffers()
	{
		// create vertex and index buffer, and related staging buffer for uploading data
		vulkan_device_->createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &vertex_buffer_, vertex_buffer_size_);
		vulkan_device_->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &vertex_staging_buffer_, vertex_buffer_size_);
		vulkan_device_->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &index_buffer_, index_buffer_size_);
		vulkan_device_->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &index_staging_buffer_, index_buffer_size_);
	}

	void UpdateIndexBuffer()
	{
		std::ifstream index_file(root_folder + "index.bin", std::ios::binary);
		if (index_file.is_open()) {
			index_file.read(reinterpret_cast<char*>(indexs_.data()), index_buffer_size_);
		}
		index_file.close();

		VK_CHECK_RESULT(index_staging_buffer_.map());
		index_staging_buffer_.copyTo(indexs_.data(), index_buffer_size_);
		index_staging_buffer_.unmap();

		VkBufferCopy copyRegion = {};
		copyRegion.size = index_buffer_size_;
		vulkan_device_->copyBuffer(&index_staging_buffer_, &index_buffer_, queue_, &copyRegion);
	}

	void UpdateVertexBuffer(std::vector<Vertex>& model_vertexs)
	{
		VK_CHECK_RESULT(vertex_staging_buffer_.map());
		vertex_staging_buffer_.copyTo(model_vertexs.data(), vertex_buffer_size_);
		vertex_staging_buffer_.unmap();

		VkBufferCopy copyRegion = {};
		copyRegion.size = vertex_buffer_size_;
		vulkan_device_->copyBuffer(&vertex_staging_buffer_, &vertex_buffer_, queue_, &copyRegion);
	}

	vks::VulkanDevice* vulkan_device_; // to create resources
	VkQueue queue_; // perform actions
	vks::Buffer vertex_buffer_; // vertex data from smpl.forward
	vks::Buffer vertex_staging_buffer_;
	vks::Buffer index_buffer_; // index data from smpl.faces
	vks::Buffer index_staging_buffer_;
	Mesh mesh_; // Here we have only one primitive
	std::vector<uint32_t> indexs_; // index is fixed
	glm::mat4 matrix_{ glm::mat4(1) }; // model trans matrix
	uint64_t vertex_count_;
	uint32_t index_count_;
	size_t vertex_buffer_size_;
	size_t index_buffer_size_;
	std::ifstream vertex_binary_file_;
};

/*
 * VulkanExample
 */
class VulkanExample : public VulkanExampleBase
{
public:
	VkPipeline pipeline{ VK_NULL_HANDLE };
	VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
	VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };
	VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };

	// Even though this sample renders multiple objects (gears), we only use single buffers
	// This is a best practices and Vulkan applications should keep the number of memory allocations as small as possible
	// Having as little buffers as possible also reduces the number of buffer binds
	struct UniformData
	{
		glm::mat4 projection;
		glm::mat4 view;
		glm::vec4 lightPos;
	} uniformData;
	vks::Buffer uniformBuffer;

	struct Pipelines {
		VkPipeline solid{ VK_NULL_HANDLE };
		VkPipeline wireframe{ VK_NULL_HANDLE };
	} pipelines;

	struct PlaySettings {
		bool pause = false;
		float speed = 1;
	} play_settings;

	bool wireframe = false;
	int frame_number_{ 0 };
	int current_frame{ 0 };
	uint32_t frame_speed_count = 0;
	std::vector<std::unique_ptr<SMPLModel>> models_;

	VulkanExample() : VulkanExampleBase()
	{
		title = "SMPL Model Rendering";
		camera.type = Camera::CameraType::lookat;
		camera.flipY = true;
		camera.setPosition(glm::vec3(0.0f, 0.0f, -3.0f));
		camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);

		commandLineParser.add("root_folder", { "--root-folder" }, 1, "Root folder of smpl model");
		commandLineParser.parse(args);
		if (commandLineParser.isSet("root_folder")) {
			root_folder = commandLineParser.getValueAsString("root_folder", default_root_folder);
		}
	}

	void SetUpFrameNumber()
	{
		std::fstream file(root_folder + "config.json");
		json j = json::parse(file);
		j["frame_number"].get_to(frame_number_);
		file.close();
	}

	void UpdateCurrentFrame(bool force = false, int step = 1)
	{
		if (force) {
			current_frame = (current_frame + step + frame_number_) % frame_number_;
		}
		else if (!play_settings.pause && play_settings.speed > 0) {
			frame_speed_count++;
			if (frame_speed_count >= 1 / play_settings.speed) {
				frame_speed_count = 0;
				current_frame = (current_frame + step + frame_number_) % frame_number_;
			}
		}
	}

	~VulkanExample()
	{
		if (device) {
			//vkDestroyPipeline(device, pipeline, nullptr);
			vkDestroyPipeline(device, pipelines.solid, nullptr);
			if (pipelines.wireframe != VK_NULL_HANDLE) {
				vkDestroyPipeline(device, pipelines.wireframe, nullptr);
			}
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
			uniformBuffer.destroy();
		}
	}

	void PrepareSMPLModel()
	{
		models_.push_back(std::make_unique<SMPLModel>(vulkanDevice, queue));
	}

	void setupDescriptors()
	{
		// We use a single descriptor set for the uniform data that contains both global matrices as well as per-gear model matrices

		// Pool
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1),
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, static_cast<uint32_t>(models_.size()));
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Layout
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			// Binding 0 : Vertex shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0)
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

		// Set
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));

		VkWriteDescriptorSet writeDescriptorSet = vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffer.descriptor);
		vkUpdateDescriptorSets(vulkanDevice->logicalDevice, 1, &writeDescriptorSet, 0, nullptr);
	}

	void preparePipelines()
	{
		// Layout
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
		// We will use push constants to push the local matrices of a primitive to the vertex shader
		VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::mat4), 0);
		// Push constant ranges are part of the pipeline layout
		pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
		pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

		//  Pipelines
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);

		// Solid rendering pipeline
		// Load shaders
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		shaderStages[0] = loadShader(getShadersPath() + "smplloading/smplloading.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "smplloading/smplloading.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		// Vertex bindings and attributes to match the vertex buffers storing the vertices for the gears
		VkVertexInputBindingDescription vertexInputBinding = {
			vks::initializers::vertexInputBindingDescription(0, sizeof(SMPLModel::Vertex), VK_VERTEX_INPUT_RATE_VERTEX)
		};
		std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
			vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SMPLModel::Vertex, position)),	// Location 0 : Position
			vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SMPLModel::Vertex, normal)),	// Location 1 : Normal
			vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SMPLModel::Vertex, color)),	// Location 2 : Color
		};
		VkPipelineVertexInputStateCreateInfo vertexInputStateCI = vks::initializers::pipelineVertexInputStateCreateInfo();
		vertexInputStateCI.vertexBindingDescriptionCount = 1;
		vertexInputStateCI.pVertexBindingDescriptions = &vertexInputBinding;
		vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
		vertexInputStateCI.pVertexAttributeDescriptions = vertexInputAttributes.data();

		VkGraphicsPipelineCreateInfo pipelineCreateInfo = vks::initializers::pipelineCreateInfo(pipelineLayout, renderPass, 0);
		pipelineCreateInfo.pVertexInputState = &vertexInputStateCI;
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCreateInfo.pStages = shaderStages.data();


		//VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline));

		// Solid rendering pipeline
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.solid));

		// Wire frame rendering pipeline
		if (deviceFeatures.fillModeNonSolid) {
			rasterizationState.polygonMode = VK_POLYGON_MODE_LINE;
			rasterizationState.lineWidth = 1.0f;
			VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.wireframe));
		}
	}



	void buildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VkClearValue clearValues[2];
		clearValues[0].color = defaultClearColor;
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		for (auto& model : models_)
			model->LoadVertexData(current_frame); // update vertex buffer data

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
		{
			renderPassBeginInfo.framebuffer = frameBuffers[i];

			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			//vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, wireframe ? pipelines.wireframe : pipelines.solid);

			// Vertices, indices and uniform data for all gears are stored in single buffers, so we only need to bind one buffer of each type and then index/offset into that for each separate gear
			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
			for (auto& model : models_)
				model->Draw(drawCmdBuffers[i], pipelineLayout);

			drawUI(drawCmdBuffers[i]);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}
	}

	void prepareUniformBuffers()
	{
		// Create the vertex shader uniform buffer block
		VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffer, sizeof(UniformData)));
		// Map persistent
		VK_CHECK_RESULT(uniformBuffer.map());
	}

	void updateUniformBuffers()
	{
		// Camera specific global matrices
		uniformData.projection = camera.matrices.perspective;
		uniformData.view = camera.matrices.view;
		uniformData.lightPos = glm::vec4(0.0f, 0.0f, 2.5f, 1.0f);

		memcpy(uniformBuffer.mapped, &uniformData, sizeof(UniformData));
	}



	void prepare()
	{
		VulkanExampleBase::prepare();
		SetUpFrameNumber(); // read config to get frame number in model
		PrepareSMPLModel(); // prepare model
		prepareUniformBuffers();
		setupDescriptors();
		preparePipelines();
		buildCommandBuffers();
		prepared = true;
	}

	void draw()
	{
		VulkanExampleBase::prepareFrame();
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
		VulkanExampleBase::submitFrame();
	}

	virtual void render()
	{
		if (!prepared)
			return;
		updateUniformBuffers();
		buildCommandBuffers();
		draw();
		UpdateCurrentFrame();
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay* overlay)
	{
		if (overlay->header("Settings")) {
			overlay->checkBox("Pause", &play_settings.pause);
			if (overlay->button("Next frame")) {
				UpdateCurrentFrame(true);
			}
			if (overlay->button("Previous frame")) {
				UpdateCurrentFrame(true, -1);
			}
			overlay->sliderInt("Frame Slider", &current_frame, 0, frame_number_ - 1);
			overlay->sliderFloat("Speed Slider", &play_settings.speed, 0, 1);
			if (overlay->inputFloat("Speed Input", &play_settings.speed, 0.001f, 3)) {
				if (play_settings.speed < 0)
					play_settings.speed = 0;
				if (play_settings.speed > 1)
					play_settings.speed = 1;
			}
			overlay->colorPicker("Background Color", defaultClearColor.float32);
			overlay->colorPicker("Human Color", human_color);
			if (overlay->checkBox("Wireframe", &wireframe)) {
				buildCommandBuffers();
			}
		}
	}

};

VULKAN_EXAMPLE_MAIN()