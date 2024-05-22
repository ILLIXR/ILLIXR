/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met:
*  * Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
*  * Redistributions in binary form must reproduce the above copyright
*    notice, this list of conditions and the following disclaimer in the
*    documentation and/or other materials provided with the distribution.
*  * Neither the name of NVIDIA CORPORATION nor the names of its
*    contributors may be used to endorse or promote products derived
*    from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
* EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
* PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
* CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
* EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
* PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
* OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "NvVulkanRenderer.h"
#include "NvLogging.h"
#include "nvbufsurface.h"

#include <unistd.h>
#include <vector>
#include <bitset>
#include <set>
#include <vulkan/vulkan_xlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#define CAT_NAME "VulkanRenderer"

using namespace std;

#define VK_CHECK(f)                                                                             \
   do {                                                                                        \
       const VkResult result = (f);                                                            \
       if (result != VK_SUCCESS) {                                                             \
           printf("Abort. %s failed at %s:%d. Result = %d\n", #f, __FILE__, __LINE__, result); \
           abort();                                                                            \
       }                                                                                       \
   } while (false)

#define CHECK(f)                                                           \
   do {                                                                   \
       if (!(f)) {                                                        \
           printf("Abort. %s failed at %s:%d\n", #f, __FILE__, __LINE__); \
           abort();                                                       \
       }                                                                  \
   } while (false)

NvVulkanRenderer::NvVulkanRenderer(const char *name, uint32_t width, uint32_t height,
                                  uint32_t x_offset, uint32_t y_offset)
   : NvElement(name, valid_fields)
{
   m_XWindow = 0;
   m_XDisplay = NULL;

   XInitThreads();
   m_XDisplay = XOpenDisplay(NULL);
   long visualMask = VisualScreenMask;
   int numberOfVisuals;
   XVisualInfo vInfoTemplate = {};
   vInfoTemplate.screen = DefaultScreen(m_XDisplay);
   XVisualInfo *visualInfo = XGetVisualInfo(m_XDisplay, visualMask, &vInfoTemplate, &numberOfVisuals);

   Colormap colormap =
       XCreateColormap(m_XDisplay, RootWindow(m_XDisplay, vInfoTemplate.screen), visualInfo->visual, AllocNone);

   XSetWindowAttributes windowAttributes = {};
   windowAttributes.colormap = colormap;
   windowAttributes.background_pixel = 0xFFFFFFFF;
   windowAttributes.border_pixel = 0;
   windowAttributes.event_mask = KeyPressMask | KeyReleaseMask | StructureNotifyMask | ExposureMask;

   m_XWindow = XCreateWindow(m_XDisplay, RootWindow(m_XDisplay, vInfoTemplate.screen), 0, 0, width,
                             height, 0, visualInfo->depth, InputOutput, visualInfo->visual,
                             CWBackPixel | CWBorderPixel | CWEventMask | CWColormap, &windowAttributes);
   CHECK(m_XWindow);
   XSelectInput(m_XDisplay, m_XWindow, ExposureMask | KeyPressMask);
   XMapWindow(m_XDisplay, m_XWindow);
   XFlush(m_XDisplay);

   // We allocate memory everytime for vkImage binded to foreign FD, destroy the image which is already presented
   // for which we keep track of older image memory.
   m_vkImageMemoryIndex = -1;
   m_oldImageIndex = -1;
}

NvVulkanRenderer*
NvVulkanRenderer::createVulkanRenderer(const char *name, uint32_t width,
                                      uint32_t height, uint32_t x_offset,
                                      uint32_t y_offset)
{
   NvVulkanRenderer *renderer = new NvVulkanRenderer(name, width, height,
                                                     x_offset, y_offset);
   if (renderer->isInError())
   {
       delete renderer;
       return NULL;
   }
   return renderer;
}


void
NvVulkanRenderer::initVulkan()
{
   createInstance();
   createSurface();
   getPhysicalDevice();
   getQueueFamilies();
   createDevice();
   createSwapChain();
   createCommandPool();
   createCommandBuffer();
   createSyncObjects();
}

#ifdef USE_VALIDATION
static
   VKAPI_ATTR VkBool32 VKAPI_CALL debugUtilsCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagsEXT message_type,
                      const VkDebugUtilsMessengerCallbackDataEXT* callback_data, void* user_data)
{
   if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
       COMP_WARN_MSG("Vulkan warning ");
   } else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
       COMP_ERROR_MSG("Vulkan error");
   } else {
       return VK_FALSE;
   }

   printf("(%d)\n%s\n%s\n\n", callback_data->messageIdNumber, callback_data->pMessageIdName, callback_data->pMessage);
   return VK_FALSE;
}
#endif

static
   MemoryTypeResult findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
   VkPhysicalDeviceMemoryProperties memoryProperties;
   vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

   MemoryTypeResult result;
   result.found = false;

   for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
       if ((typeFilter & (1 << i)) && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
           result.typeIndex = i;
           result.found = true;
           break;
       }
   }
   return result;
}

void
NvVulkanRenderer::createSurface()
{
   VkXlibSurfaceCreateInfoKHR createInfo;
   createInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
   createInfo.pNext = NULL;
   createInfo.flags = 0;
   createInfo.dpy = m_XDisplay;
   createInfo.window = m_XWindow;

   VK_CHECK(vkCreateXlibSurfaceKHR(m_instance, &createInfo, NULL, &m_surface));
   COMP_INFO_MSG("Vulkan surface created\n");
}

void
NvVulkanRenderer::createInstance()
{
   VkApplicationInfo appInfo{};
   appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
   appInfo.pApplicationName = "Sample";
   appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
   appInfo.pEngineName = "";
   appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
   appInfo.apiVersion = VK_API_VERSION_1_2;

#ifdef USE_VALIDATION
   VkDebugUtilsMessengerCreateInfoEXT debugUtilsCreateInfo{};
   debugUtilsCreateInfo.pNext = nullptr;
   debugUtilsCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
   debugUtilsCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
   debugUtilsCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
   debugUtilsCreateInfo.pfnUserCallback = debugUtilsCallback;
   debugUtilsCreateInfo.pUserData = nullptr;

   const std::vector<VkValidationFeatureEnableEXT> enabledFeatures{
       VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,                       //
       VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT,  //
       // VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,                     //
       // VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT,                       //
       VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,  //
   };

   VkValidationFeaturesEXT validationFeatures{};
   validationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
   validationFeatures.pNext = &debugUtilsCreateInfo;
   validationFeatures.enabledValidationFeatureCount = ui32Size(enabledFeatures);
   validationFeatures.pEnabledValidationFeatures = enabledFeatures.data();
   validationFeatures.disabledValidationFeatureCount = 0;
   validationFeatures.pDisabledValidationFeatures = nullptr;

   const std::string validationLayerName = "VK_LAYER_KHRONOS_validation";
   std::vector<const char*> enabledLayers{validationLayerName.c_str()};
   void* pNext = &validationFeatures;
#else
   std::vector<const char*> enabledLayers{};
   void* pNext = nullptr;
#endif

   VkInstanceCreateInfo instanceCreateInfo{};
   instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
   instanceCreateInfo.pApplicationInfo = &appInfo;
   instanceCreateInfo.enabledExtensionCount = ui32Size(m_instanceExtensions);
   instanceCreateInfo.ppEnabledExtensionNames = m_instanceExtensions.data();
   instanceCreateInfo.enabledLayerCount = ui32Size(enabledLayers);
   instanceCreateInfo.ppEnabledLayerNames = enabledLayers.data();
   instanceCreateInfo.pNext = pNext;

   VK_CHECK(vkCreateInstance(&instanceCreateInfo, nullptr, &m_instance));

#ifdef USE_VALIDATION
   auto vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");
   if (vkCreateDebugUtilsMessengerEXT) {
       VK_CHECK(vkCreateDebugUtilsMessengerEXT(m_instance, &debugUtilsCreateInfo, nullptr, &m_debugMessenger));
   }
#endif
   COMP_INFO_MSG("Vulkan instance created");
}

PFN_vkVoidFunction NvVulkanRenderer::getInstanceFunction(VkInstance instance, const char* name)
{
   PFN_vkVoidFunction f = vkGetInstanceProcAddr(instance, name);
   if (f == nullptr) {
       COMP_ERROR_MSG("Could not get instance function pointer");
       fprintf(stderr, "%s", name);
   }
   return f;
}

void
NvVulkanRenderer::getPhysicalDevice()
{
   uint32_t deviceCount = 0;
   vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
   CHECK(deviceCount);

   std::vector<VkPhysicalDevice> devices(deviceCount);
   vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());
   m_physicalDevice = devices[0];
   CHECK(m_physicalDevice != VK_NULL_HANDLE);
}

void
NvVulkanRenderer::getQueueFamilies()
{
   uint32_t queueFamilyCount = 0;
   vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
   std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
   vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies.data());

   QueueFamilyIndices indices;
   for (unsigned int i = 0; i < queueFamilies.size(); ++i) {
       if (queueFamilies[i].queueCount > 0 && queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
           indices.graphicsFamily = i;
       }

       if (queueFamilies[i].queueCount > 0 && queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
           indices.computeFamily = i;
       }

       VkBool32 presentSupport = false;
       vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, i, m_surface, &presentSupport);

       if (presentSupport) {
           indices.presentFamily = i;
       }
   }

   m_queueFamilyIndices = indices;
}

void
NvVulkanRenderer::createDevice()
{
   const std::set<int> uniqueQueueFamilies = {m_queueFamilyIndices.graphicsFamily, m_queueFamilyIndices.computeFamily,
                                              m_queueFamilyIndices.presentFamily};

   std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
   const float queuePriority = 1.0f;
   for (int queueFamily : uniqueQueueFamilies) {
       VkDeviceQueueCreateInfo queueCreateInfo{};
       queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
       queueCreateInfo.queueFamilyIndex = queueFamily;
       queueCreateInfo.queueCount = 1;
       queueCreateInfo.pQueuePriorities = &queuePriority;
       queueCreateInfos.push_back(queueCreateInfo);
   }

   VkPhysicalDeviceFeatures deviceFeatures{};
   VkPhysicalDeviceVulkan12Features device12Features{};
   device12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

   VkDeviceCreateInfo createInfo{};
   createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
   createInfo.pNext = &device12Features;
   createInfo.queueCreateInfoCount = ui32Size(queueCreateInfos);
   createInfo.pQueueCreateInfos = queueCreateInfos.data();
   createInfo.pEnabledFeatures = &deviceFeatures;
   createInfo.enabledExtensionCount = ui32Size(m_deviceExtensions);
   createInfo.ppEnabledExtensionNames = m_deviceExtensions.data();
   createInfo.enabledLayerCount = ui32Size(m_validationLayers);
   createInfo.ppEnabledLayerNames = m_validationLayers.data();

   VK_CHECK(vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device));

   vkGetDeviceQueue(m_device, m_queueFamilyIndices.graphicsFamily, 0, &m_graphicsQueue);
   vkGetDeviceQueue(m_device, m_queueFamilyIndices.presentFamily, 0, &m_presentQueue);

   COMP_INFO_MSG("Vulkan device created\n");
}

void
NvVulkanRenderer::setSize(uint32_t width, uint32_t height)
{
   m_windowWidth = width;
   m_windowHeight = height;
   return;
}

static
   VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
   for (const auto& availableFormat : availableFormats) {
       if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
           return availableFormat;
       }
   }

   return availableFormats[0];
}

static
   VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
   for (const auto& availablePresentMode : availablePresentModes) {
       if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
           return availablePresentMode;
       }
   }

   return VK_PRESENT_MODE_IMMEDIATE_KHR;
}

static
   VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height) {
   if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
       return capabilities.currentExtent;
   } else {
       VkExtent2D actualExtent = {
           static_cast<uint32_t>(width),
           static_cast<uint32_t>(height)
       };

#define CLAMP(x, min, max) \
       x = x > max ? max : x; \
       x = x < min ? min : x;

       CLAMP(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
       CLAMP(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

#undef CLAMP
       return actualExtent;
   }
}

static
   SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
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

void
NvVulkanRenderer::createSwapChain()
{
   SwapChainSupportDetails swapChainSupport = querySwapChainSupport(m_physicalDevice, m_surface);

   VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
   VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
   VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities, m_windowWidth, m_windowHeight);

   uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
   if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
       imageCount = swapChainSupport.capabilities.maxImageCount;
   }

   VkSwapchainCreateInfoKHR createInfo{};
   createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
   createInfo.surface = m_surface;

   createInfo.minImageCount = imageCount;
   createInfo.imageFormat = surfaceFormat.format;
   createInfo.imageColorSpace = surfaceFormat.colorSpace;
   createInfo.imageExtent = extent;
   createInfo.imageArrayLayers = 1;
   createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

   uint32_t queueFamilyIndices[] = {(uint32_t)m_queueFamilyIndices.graphicsFamily, (uint32_t)m_queueFamilyIndices.presentFamily};

   if (m_queueFamilyIndices.graphicsFamily != m_queueFamilyIndices.presentFamily) {
       createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
       createInfo.queueFamilyIndexCount = 2;
       createInfo.pQueueFamilyIndices = queueFamilyIndices;
   } else {
       createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
   }

   createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
   createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
   createInfo.presentMode = presentMode;
   createInfo.clipped = VK_TRUE;

   VK_CHECK(vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapChain));

   vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, nullptr);
   m_swapChainImages.resize(imageCount);
   m_vkImageMemory.resize(imageCount);
   vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, m_swapChainImages.data());

   m_swapChainImageFormat = surfaceFormat.format;
   m_swapChainExtent = extent;
   COMP_INFO_MSG("Vulkan swapchain created");
}

void
NvVulkanRenderer::createCommandBuffer() {
   VkCommandBufferAllocateInfo allocInfo{};
   allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
   allocInfo.commandPool = m_commandPool;
   allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
   allocInfo.commandBufferCount = 1;

   VK_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, &m_commandBuffer));
}

void
NvVulkanRenderer::createSyncObjects() {
   VkSemaphoreCreateInfo semaphoreInfo{};
   semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

   VkFenceCreateInfo fenceInfo{};
   fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
   fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

   VK_CHECK(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphore));
   VK_CHECK(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphore));
   VK_CHECK(vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFence));

}

void
NvVulkanRenderer::createCommandPool() {
   VkCommandPoolCreateInfo poolInfo{};
   poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
   poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
   poolInfo.queueFamilyIndex = m_queueFamilyIndices.graphicsFamily;

   VK_CHECK(vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool));
}

void
NvVulkanRenderer::recordCommandBuffer(VkImage image, uint32_t imageIndex)
{
   VkCommandBufferBeginInfo beginInfo{};
   beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

   VK_CHECK(vkBeginCommandBuffer(m_commandBuffer, &beginInfo));

   VkImageCopy copyRegion{};
   copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
   copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
   copyRegion.extent = {m_windowWidth, m_windowHeight, 1};
   copyRegion.srcOffset = {0, 0, 0};
   copyRegion.dstOffset = {0, 0, 0};

   vkCmdCopyImage(
       m_commandBuffer, image, VK_IMAGE_LAYOUT_GENERAL, m_swapChainImages[imageIndex], VK_IMAGE_LAYOUT_GENERAL, 1, &copyRegion);

   VK_CHECK(vkEndCommandBuffer(m_commandBuffer));
}

void
NvVulkanRenderer::displayFrame(VkImage image)
{
   VkResult err;

   VK_CHECK(vkWaitForFences(m_device, 1, &m_inFlightFence, VK_TRUE, UINT64_MAX));
   VK_CHECK(vkResetFences(m_device, 1, &m_inFlightFence));

   uint32_t imageIndex;
   err = vkAcquireNextImageKHR(m_device, m_swapChain, UINT64_MAX, m_imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
   if (err != VK_SUBOPTIMAL_KHR)
   {
       /* m_swapChain is not as optimal as it could be, but the platform's presentation engine
        * will still present the image correctly, hence do not fail on VK_SUBOPTIMAL_KHR */
       VK_CHECK(err);
   }

   VK_CHECK(vkResetCommandBuffer(m_commandBuffer, /*VkCommandBufferResetFlagBits*/ 0));
   recordCommandBuffer(image, imageIndex);

   VkSubmitInfo submitInfo{};
   submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

   VkSemaphore waitSemaphores[] = {m_imageAvailableSemaphore};
   VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
   submitInfo.waitSemaphoreCount = 1;
   submitInfo.pWaitSemaphores = waitSemaphores;
   submitInfo.pWaitDstStageMask = waitStages;

   submitInfo.commandBufferCount = 1;
   submitInfo.pCommandBuffers = &m_commandBuffer;

   VkSemaphore signalSemaphores[] = {m_renderFinishedSemaphore};
   submitInfo.signalSemaphoreCount = 1;
   submitInfo.pSignalSemaphores = signalSemaphores;

   VK_CHECK(vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFence));

   VkPresentInfoKHR presentInfo{};
   presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

   presentInfo.waitSemaphoreCount = 1;
   presentInfo.pWaitSemaphores = signalSemaphores;

   VkSwapchainKHR swapChains[] = {m_swapChain};
   presentInfo.swapchainCount = 1;
   presentInfo.pSwapchains = swapChains;

   presentInfo.pImageIndices = &imageIndex;

   err = vkQueuePresentKHR(m_presentQueue, &presentInfo);
   if (err == VK_SUBOPTIMAL_KHR)
   {
       /* m_swapChain is not as optimal as it could be, but the platform's presentation engine
        * will still present the image correctly, hence do not fail on VK_SUBOPTIMAL_KHR */
   } else if (err == VK_ERROR_SURFACE_LOST_KHR) {
       /* Should we attempt to do something here or on hotplug detection? */
   } else {
       VK_CHECK(err);
   }
}

void
NvVulkanRenderer::createVkImageFromFd(int fd)
{
   COMP_DEBUG_MSG("\nCreating vk image from fd");

   VkImage vkImage;
   uint32_t width = m_windowWidth;
   uint32_t height = m_windowHeight;

   {  // create vk image
       VkExternalMemoryImageCreateInfo dmaBufExternalMemoryImageCreateInfo{};
       dmaBufExternalMemoryImageCreateInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
       dmaBufExternalMemoryImageCreateInfo.pNext = nullptr;
       dmaBufExternalMemoryImageCreateInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

       VkImageCreateInfo dmaBufImageCreateInfo{};
       dmaBufImageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
       dmaBufImageCreateInfo.pNext = &dmaBufExternalMemoryImageCreateInfo;
       dmaBufImageCreateInfo.flags = 0;
       dmaBufImageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
       dmaBufImageCreateInfo.format = m_imageFormat;
       dmaBufImageCreateInfo.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
       dmaBufImageCreateInfo.mipLevels = 1;
       dmaBufImageCreateInfo.arrayLayers = 1;
       dmaBufImageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
       dmaBufImageCreateInfo.tiling = m_imageTiling;
       dmaBufImageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
       dmaBufImageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
       dmaBufImageCreateInfo.queueFamilyIndexCount = 0;
       dmaBufImageCreateInfo.pQueueFamilyIndices = nullptr;
       dmaBufImageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // NOTE Check spec once more!
       VK_CHECK(vkCreateImage(m_device, &dmaBufImageCreateInfo, nullptr, &vkImage));
   }

   {  // allocate and bind
       const int duppedFd = dup(fd);
       (void)(duppedFd);

       auto vkGetMemoryFdPropertiesKHR = (PFN_vkGetMemoryFdPropertiesKHR)getInstanceFunction(m_instance, "vkGetMemoryFdPropertiesKHR");
       CHECK(vkGetMemoryFdPropertiesKHR);

       VkMemoryFdPropertiesKHR dmaBufMemoryProperties{};
       dmaBufMemoryProperties.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR;
       dmaBufMemoryProperties.pNext = nullptr;
       VK_CHECK(vkGetMemoryFdPropertiesKHR(m_device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, duppedFd, &dmaBufMemoryProperties));
       string str = "Fd memory memoryTypeBits: b" + std::bitset<8>(dmaBufMemoryProperties.memoryTypeBits).to_string();
       COMP_DEBUG_MSG(str);

       VkMemoryRequirements imageMemoryRequirements{};
       vkGetImageMemoryRequirements(m_device, vkImage, &imageMemoryRequirements);
       str = "Image memoryTypeBits: b" +  std::bitset<8>(imageMemoryRequirements.memoryTypeBits).to_string();
       COMP_DEBUG_MSG(str);

       const uint32_t bits = dmaBufMemoryProperties.memoryTypeBits & imageMemoryRequirements.memoryTypeBits;
       CHECK(bits != 0);

       const MemoryTypeResult memoryTypeResult = findMemoryType(m_physicalDevice, bits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
       CHECK(memoryTypeResult.found);
       str = "Memory type index: " + to_string(memoryTypeResult.typeIndex);
       COMP_DEBUG_MSG(str);

       VkMemoryDedicatedAllocateInfo dedicatedAllocateInfo{};
       dedicatedAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
       dedicatedAllocateInfo.image = vkImage;
       VkImportMemoryFdInfoKHR importFdInfo{};
       importFdInfo.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
       importFdInfo.pNext = &dedicatedAllocateInfo;
       importFdInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
       importFdInfo.fd = duppedFd;

       str = "Memory size = " + to_string(imageMemoryRequirements.size);
       COMP_DEBUG_MSG(str);

       m_oldImageIndex = m_vkImageMemoryIndex;
       int size = (int)m_vkImageMemory.size();
       m_vkImageMemoryIndex += 1;
       m_vkImageMemoryIndex = m_vkImageMemoryIndex % size;
       VkMemoryAllocateInfo memoryAllocateInfo{};
       memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
       memoryAllocateInfo.pNext = &importFdInfo;
       memoryAllocateInfo.allocationSize = imageMemoryRequirements.size;
       memoryAllocateInfo.memoryTypeIndex = memoryTypeResult.typeIndex;
       VK_CHECK(vkAllocateMemory(m_device, &memoryAllocateInfo, nullptr, &m_vkImageMemory[m_vkImageMemoryIndex]));

       VK_CHECK(vkBindImageMemory(m_device, vkImage, m_vkImageMemory[m_vkImageMemoryIndex], 0));
   }

   {
       displayFrame(vkImage);
   }

   if (m_oldImageIndex >= 0) {
       vkDestroyImage(m_device, vkImage, nullptr);
       vkFreeMemory(m_device, m_vkImageMemory[m_oldImageIndex], nullptr);
   }

   COMP_DEBUG_MSG("Vulkan image from fd created\n\n");
}

void
NvVulkanRenderer::render(int fd)
{
   createVkImageFromFd(fd);
}

NvVulkanRenderer::~NvVulkanRenderer()
{
   vkDestroySemaphore(m_device, m_renderFinishedSemaphore, nullptr);
   vkDestroySemaphore(m_device, m_imageAvailableSemaphore, nullptr);
   vkDestroyFence(m_device, m_inFlightFence, nullptr);

   vkDestroyCommandPool(m_device, m_commandPool, nullptr);
   vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);
   vkDestroyDevice(m_device, nullptr);
   vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
   vkDestroyInstance(m_instance, nullptr);
}