// SPDX-License-Identifier: GPL-3.0-or-later
#include "api_run.hpp"
#include "composite.inc"
#include "movingTerrain.inc"
#include "screen.inc"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <functional>
#include <sstream>
#include <vulkan/vulkan.h>
#include <windows.h>

using namespace api_test;
namespace {
void check(VkResult r, const char *name) {
    if (r != VK_SUCCESS)
        throw std::runtime_error(std::string(name) + " returned " + std::to_string(r));
}
struct Target {
    VkImage image{};
    VkImageView view{};
};
struct Lab {
    bool matchedPolling = false;
    VkInstance instance{};
    VkPhysicalDevice gpu{};
    VkDevice device{};
    VkQueue queue{};
    VkPhysicalDeviceProperties props{};
    uint32_t family{}, validBits{};
    std::vector<std::function<void()>> cleanup;
    std::vector<VkDeviceMemory> allocations;
    Target scene, output;
    VkBuffer staging{};
    VkDeviceMemory hostMemory{};
    VkDescriptorSet descriptors{};
    VkPipelineLayout layout{};
    VkPipeline terrain{}, blit{};
    VkQueryPool queries{};
    VkCommandBuffer cmd{};
    VkFence fence{};
    ~Lab() {
        if (device) {
            vkDeviceWaitIdle(device);
            for (auto it = cleanup.rbegin(); it != cleanup.rend(); ++it)
                (*it)();
            for (auto m : allocations)
                vkFreeMemory(device, m, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if (instance)
            vkDestroyInstance(instance, nullptr);
    }
    void init() {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "WordFinance graphics lab";
        app.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ici.pApplicationInfo = &app;
        check(vkCreateInstance(&ici, nullptr, &instance), "vkCreateInstance");
        uint32_t count = 0;
        check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "Enumerate device count");
        if (!count)
            throw std::runtime_error("No Vulkan adapter");
        std::vector<VkPhysicalDevice> devices(count);
        check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "Enumerate devices");
        for (auto candidate : devices) {
            vkGetPhysicalDeviceProperties(candidate, &props);
            if (props.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
                props.deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
                continue;
            if (props.apiVersion < VK_API_VERSION_1_3)
                continue;
            uint32_t n = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &n, nullptr);
            std::vector<VkQueueFamilyProperties> families(n);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &n, families.data());
            for (uint32_t i = 0; i < n; ++i)
                if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && families[i].timestampValidBits) {
                    gpu = candidate;
                    family = i;
                    validBits = families[i].timestampValidBits;
                    break;
                }
            if (gpu)
                break;
        }
        if (!gpu)
            throw std::runtime_error("No hardware Vulkan 1.3 graphics queue with timestamps");
        VkPhysicalDeviceDynamicRenderingFeatures dynamic{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &dynamic;
        vkGetPhysicalDeviceFeatures2(gpu, &features);
        if (!dynamic.dynamicRendering)
            throw std::runtime_error("Dynamic rendering unavailable");
        float priority = 1;
        VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        q.queueFamilyIndex = family;
        q.queueCount = 1;
        q.pQueuePriorities = &priority;
        VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        di.pNext = &dynamic;
        di.queueCreateInfoCount = 1;
        di.pQueueCreateInfos = &q;
        check(vkCreateDevice(gpu, &di, nullptr, &device), "vkCreateDevice");
        vkGetDeviceQueue(device, family, 0, &queue);

        scene = target(VK_IMAGE_USAGE_SAMPLED_BIT);
        output = target(VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = width * height * 4;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        check(vkCreateBuffer(device, &bi, nullptr, &staging), "Readback buffer");
        cleanup.push_back([this] { vkDestroyBuffer(device, staging, nullptr); });
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(device, staging, &req);
        hostMemory =
            memory(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(vkBindBufferMemory(device, staging, hostMemory, 0), "Bind readback memory");
        setupGraphics();
        VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pi.queueFamilyIndex = family;
        pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VkCommandPool pool{};
        check(vkCreateCommandPool(device, &pi, nullptr, &pool), "Command pool");
        cleanup.push_back([this, pool] { vkDestroyCommandPool(device, pool, nullptr); });
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = pool;
        ai.commandBufferCount = 1;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        check(vkAllocateCommandBuffers(device, &ai, &cmd), "Command buffer");
        VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        qi.queryCount = 2;
        qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
        check(vkCreateQueryPool(device, &qi, nullptr, &queries), "Timestamp pool");
        cleanup.push_back([this] { vkDestroyQueryPool(device, queries, nullptr); });
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        check(vkCreateFence(device, &fi, nullptr, &fence), "Fence");
        cleanup.push_back([this] { vkDestroyFence(device, fence, nullptr); });
    }
    VkDeviceMemory memory(VkMemoryRequirements req, VkMemoryPropertyFlags flags) {
        VkPhysicalDeviceMemoryProperties mp{};
        vkGetPhysicalDeviceMemoryProperties(gpu, &mp);
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
            if ((req.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & flags) == flags) {
                VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                ai.memoryTypeIndex = i;
                ai.allocationSize = req.size;
                VkDeviceMemory m{};
                check(vkAllocateMemory(device, &ai, nullptr, &m), "Allocate memory");
                allocations.push_back(m);
                return m;
            }
        throw std::runtime_error("No suitable memory type");
    }
    Target target(VkImageUsageFlags usage) {
        Target t{};
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = VK_FORMAT_R8G8B8A8_UNORM;
        ci.extent = {width, height, 1};
        ci.arrayLayers = ci.mipLevels = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = usage | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        check(vkCreateImage(device, &ci, nullptr, &t.image), "Color image");
        cleanup.push_back([this, img = t.image] { vkDestroyImage(device, img, nullptr); });
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device, t.image, &req);
        check(vkBindImageMemory(device, t.image, memory(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), 0),
              "Bind color image");
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = t.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = ci.format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        check(vkCreateImageView(device, &vi, nullptr, &t.view), "Image view");
        cleanup.push_back([this, v = t.view] { vkDestroyImageView(device, v, nullptr); });
        return t;
    }
    template <size_t N> VkShaderModule module(const uint32_t (&words)[N]) {

        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = sizeof(words);
        ci.pCode = words;
        VkShaderModule mod{};
        check(vkCreateShaderModule(device, &ci, nullptr, &mod), "Shader module");
        cleanup.push_back([this, mod] { vkDestroyShaderModule(device, mod, nullptr); });
        return mod;
    }
    void setupGraphics() {
        VkDescriptorSetLayoutBinding bindings[] = {
            {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo si{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        si.bindingCount = 2;
        si.pBindings = bindings;
        VkDescriptorSetLayout setLayout{};
        check(vkCreateDescriptorSetLayout(device, &si, nullptr, &setLayout), "Set layout");
        cleanup.push_back(
            [this, setLayout] { vkDestroyDescriptorSetLayout(device, setLayout, nullptr); });
        VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1},
                                        {VK_DESCRIPTOR_TYPE_SAMPLER, 1}};
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pi.maxSets = 1;
        pi.poolSizeCount = 2;
        pi.pPoolSizes = sizes;
        VkDescriptorPool pool{};
        check(vkCreateDescriptorPool(device, &pi, nullptr, &pool), "Descriptor pool");
        cleanup.push_back([this, pool] { vkDestroyDescriptorPool(device, pool, nullptr); });
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = pool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &setLayout;
        check(vkAllocateDescriptorSets(device, &ai, &descriptors), "Descriptor set");
        VkSamplerCreateInfo sm{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sm.minFilter = sm.magFilter = VK_FILTER_LINEAR;
        sm.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sm.addressModeU = sm.addressModeV = sm.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sm.maxLod = VK_LOD_CLAMP_NONE;
        VkSampler sampler{};
        check(vkCreateSampler(device, &sm, nullptr, &sampler), "Linear sampler");
        cleanup.push_back([this, sampler] { vkDestroySampler(device, sampler, nullptr); });
        VkDescriptorImageInfo infos[] = {
            {VK_NULL_HANDLE, scene.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED}};
        VkWriteDescriptorSet writes[2]{};
        for (unsigned i = 0; i < 2; ++i) {
            auto &w = writes[i];
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = descriptors;
            w.dstBinding = i;
            w.descriptorCount = 1;
            w.descriptorType = bindings[i].descriptorType;
            w.pImageInfo = &infos[i];
        }
        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
        VkPushConstantRange pc{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
        VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        li.pushConstantRangeCount = 1;
        li.pPushConstantRanges = &pc;
        li.setLayoutCount = 1;
        li.pSetLayouts = &setLayout;
        check(vkCreatePipelineLayout(device, &li, nullptr, &layout), "Pipeline layout");
        cleanup.push_back([this] { vkDestroyPipelineLayout(device, layout, nullptr); });
        auto vs = module(spv_screen);
        terrain = pipeline(vs, module(spv_movingTerrain), "movingTerrain");
        blit = pipeline(vs, module(spv_composite), "composite");
    }
    VkPipeline pipeline(VkShaderModule vs, VkShaderModule ps, const char *entry) {
        VkPipelineShaderStageCreateInfo shaders[2]{};
        for (auto &s : shaders)
            s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaders[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaders[0].module = vs;
        shaders[0].pName = "screen";
        shaders[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaders[1].module = ps;
        shaders[1].pName = entry;
        VkPipelineVertexInputStateCreateInfo vi{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ia{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport vp{0, 0, float(width), float(height), 0, 1};
        VkRect2D rect{{0, 0}, {width, height}};
        VkPipelineViewportStateCreateInfo viewport{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = viewport.scissorCount = 1;
        viewport.pViewports = &vp;
        viewport.pScissors = &rect;
        VkPipelineRasterizationStateCreateInfo rs{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.lineWidth = 1;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        VkPipelineMultisampleStateCreateInfo ms{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blend{};
        blend.colorWriteMask = 0xf;
        VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        bs.attachmentCount = 1;
        bs.pAttachments = &blend;
        VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = &format;
        VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        ci.pNext = &rendering;
        ci.stageCount = 2;
        ci.pStages = shaders;
        ci.pVertexInputState = &vi;
        ci.pInputAssemblyState = &ia;
        ci.pViewportState = &viewport;
        ci.pRasterizationState = &rs;
        ci.pMultisampleState = &ms;
        ci.pColorBlendState = &bs;
        ci.layout = layout;
        VkPipeline pipe{};
        check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipe),
              "Graphics pipeline");
        cleanup.push_back([this, pipe] { vkDestroyPipeline(device, pipe, nullptr); });
        return pipe;
    }
    void transition(VkImage img, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags src,
                    VkPipelineStageFlags dst, VkAccessFlags before, VkAccessFlags after) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = from;
        b.newLayout = to;
        b.srcAccessMask = before;
        b.dstAccessMask = after;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = img;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, src, dst, 0, 0, nullptr, 0, nullptr, 1, &b);
    }
    void draw(Target &dst, VkPipeline pipe, unsigned repeats) {
        transition(dst.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color.imageView = dst.view;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        ri.renderArea.extent = {width, height};
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &color;
        vkCmdBeginRendering(cmd, &ri);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        for (unsigned i = 0; i < repeats; ++i)
            vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRendering(cmd);
    }
    Time frame(unsigned i, bool capture) {
        const auto begin = std::chrono::steady_clock::now();
        check(vkResetCommandBuffer(cmd, 0), "Reset commands");
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(cmd, &bi), "Begin commands");
        vkCmdResetQueryPool(cmd, queries, 0, 2);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queries, 0);
        float values[4] = {camera(i), 0, 0, 0};
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(values), values);
        draw(scene, terrain, passes(i));
        transition(scene.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &descriptors, 0,
                                nullptr);
        draw(output, blit, 1);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queries, 1);
        if (capture) {
            transition(output.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {width, height, 1};
            vkCmdCopyImageToBuffer(cmd, output.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1,
                                   &region);
            VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
                                 &b, 0, nullptr, 0, nullptr);
        }
        check(vkEndCommandBuffer(cmd), "End commands");
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        check(vkResetFences(device, 1, &fence), "Reset fence");
        check(vkQueueSubmit(queue, 1, &si, fence), "Submit frame");
        if (matchedPolling) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            for (;;) {
                const auto status = vkGetFenceStatus(device, fence);
                if (status == VK_SUCCESS)
                    break;
                if (status != VK_NOT_READY)
                    check(status, "Poll GPU completion");
                if (std::chrono::steady_clock::now() >= deadline)
                    throw std::runtime_error("GPU fence timed out");
                Sleep(1);
            }
        } else {
            check(vkWaitForFences(device, 1, &fence, VK_TRUE, 15000000000ull), "Wait for GPU");
        }
        uint64_t ts[2]{};
        check(vkGetQueryPoolResults(device, queries, 0, 2, sizeof(ts), ts, sizeof(uint64_t),
                                    VK_QUERY_RESULT_64_BIT),
              "Read GPU timestamps");
        auto ticks = ts[1] - ts[0];
        if (validBits < 64)
            ticks &= (uint64_t(1) << validBits) - 1;
        if (!ticks)
            throw std::runtime_error("Zero timestamp interval");
        return {
            double(ticks) * props.limits.timestampPeriod / 1e6,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count()};
    }
};
} // namespace
int wmain(int argc, wchar_t **argv) {
    try {
        if (argc != 2 && !(argc == 3 && std::wstring(argv[2]) == L"--match-poll"))
            throw std::runtime_error("Usage: legacy-vulkan NEW_CAPTURE_DIRECTORY [--match-poll]");
        std::filesystem::path dir(argv[1]);
        if (!std::filesystem::create_directory(dir))
            throw std::runtime_error("Choose a new directory");
        Lab lab;
        lab.matchedPolling = argc == 3;
        lab.init();
        for (unsigned n = 0; n < warmups; ++n)
            lab.frame(0, false);
        std::vector<Time> times;
        for (unsigned i = 0; i < frames; ++i)
            times.push_back(lab.frame(i, false));
        for (unsigned i = 0; i < frames; ++i) {
            lab.frame(i, true);
            void *ptr = nullptr;
            check(vkMapMemory(lab.device, lab.hostMemory, 0, width * height * 4, 0, &ptr),
                  "Map readback");
            save(dir, i, ptr);
            vkUnmapMemory(lab.device, lab.hostMemory);
        }
        std::ostringstream name;
        name << std::quoted(lab.props.deviceName);
        print("Vulkan", name.str(), times);
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
